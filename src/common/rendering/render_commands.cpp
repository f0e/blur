#include "render_commands.h"
#include "common/config_presets.h"

namespace {
	// audio: trim each stream to the render's cut points and apply the timescale
	// (either by resampling to change pitch, or chained atempo to preserve it)
	void append_audio_filter_args(
		std::vector<std::string>& args,
		const u::VideoInfo& video_info,
		const BlurSettings& settings,
		size_t start_frame,
		size_t end_frame
	) {
		if (video_info.audio_sample_rates.empty())
			return;

		std::string complex_filter;
		for (size_t i = 0; i < video_info.audio_sample_rates.size(); i++) {
			if (i > 0)
				complex_filter += ";";

			// @todo: i still dont know if audio will be perfectly synced but it seems like an endless rabbit hole
			int sample_rate = video_info.audio_sample_rates[i];
			double audio_start_time = video_info.audio_start_times[i];
			double frame_duration = static_cast<double>(video_info.fps_den) / video_info.fps_num;

			auto start_sample = static_cast<size_t>(
				((start_frame * frame_duration + video_info.video_start_time - audio_start_time) * sample_rate) + 0.5
			);
			auto end_sample = static_cast<size_t>(
				((end_frame * frame_duration + video_info.video_start_time - audio_start_time) * sample_rate) + 0.5
			);

			// build the middle part of the filter - everything between asetpts and the output label
			std::string timescale_filter;
			if (settings.timescale) {
				float speed = settings.output_timescale / settings.input_timescale;

				if (settings.output_timescale_audio_pitch) {
					int shifted_rate = static_cast<int>(std::round(sample_rate * speed));
					timescale_filter = std::format(",asetrate={},aresample={}", shifted_rate, sample_rate);
				}
				else {
					std::string atempo;
					float s = std::clamp(speed, 0.25f, 100.f);
					while (s > 2.0f) {
						atempo += "atempo=2.0,";
						s /= 2.0f;
					}
					while (s < 0.5f) {
						atempo += "atempo=0.5,";
						s /= 0.5f;
					}
					atempo += std::format("atempo={:.6f}", s);
					timescale_filter = "," + atempo;
				}
			}

			complex_filter += std::format(
				"[1:a:{}]atrim=start_pts={}:end_pts={},asetpts=PTS-STARTPTS{}[a{}]",
				i,
				start_sample,
				end_sample,
				timescale_filter,
				i
			);
		}

		args.insert(args.end(), { "-filter_complex", complex_filter });

		for (size_t i = 0; i < video_info.audio_sample_rates.size(); i++) {
			args.insert(args.end(), { "-map", std::format("[a{}]", i) });
		}
	}

	// carry the source's colour metadata through so the output isn't reinterpreted
	void append_colour_param_args(std::vector<std::string>& args, const u::VideoInfo& video_info) {
		std::vector<std::string> params;

		if (video_info.color_range) {
			std::string range = *video_info.color_range == "pc" ? "full" : "limited";
			params.emplace_back("range=" + range);
		}

		if (video_info.color_space)
			params.emplace_back("colorspace=" + *video_info.color_space);

		if (video_info.color_transfer)
			params.emplace_back("color_trc=" + *video_info.color_transfer);

		if (video_info.color_primaries)
			params.emplace_back("color_primaries=" + *video_info.color_primaries);

		if (params.empty())
			return;

		std::string filter =
			"setparams=" +
			std::accumulate(
				std::next(params.begin()), params.end(), params[0], [](const std::string& a, const std::string& b) {
					return a + ":" + b;
				}
			);

		args.insert(args.end(), { "-vf", filter });

		if (video_info.pix_fmt) {
			args.insert(args.end(), { "-pix_fmt", *video_info.pix_fmt });
		}
	}

	// encoder settings: a raw ffmpeg override string if given, else the preset's params
	void append_encoding_args(
		std::vector<std::string>& args, const BlurSettings& settings, const GlobalAppSettings& app_settings
	) {
		if (!settings.advanced.ffmpeg_override.empty()) {
			auto override_args = u::ffmpeg_string_to_args(settings.advanced.ffmpeg_override);
			for (const auto& arg : override_args) {
				args.push_back(arg);
			}
		}
		else {
			auto preset_args = config_presets::get_preset_params(
				settings.gpu_encoding ? app_settings.gpu_type : "cpu",
				u::to_lower(settings.encode_preset.empty() ? "h264" : settings.encode_preset),
				settings.quality
			);

			for (const auto& arg : preset_args) {
				args.push_back(arg);
			}
		}
	}
}

tl::expected<nlohmann::json, std::string> rendering::detail::merge_settings(
	const BlurSettings& blur_settings, const GlobalAppSettings& app_settings
) {
	auto settings_json = blur_settings.to_json();
	if (!settings_json)
		return settings_json;

	auto app_json = app_settings.to_json();
	if (!app_json)
		return tl::unexpected(app_json.error());

	settings_json->update(*app_json);
	return settings_json;
}

std::vector<std::string> rendering::detail::build_vspipe_base_args(
	const std::filesystem::path& input_path, const nlohmann::json& merged_settings
) {
	std::string path_str = u::path_to_string(input_path);
	std::ranges::replace(path_str, '\\', '/');

	std::vector<std::string> args = {
		"-p", "-c", "y4m", "-a", "video_path=" + path_str, "-a", "settings=" + merged_settings.dump(),
	};

#ifdef __APPLE__
	args.insert(args.end(), { "-a", std::format("macos_bundled={}", blur.used_installer ? "true" : "false") });
#endif
#ifdef _WIN32
	args.insert(args.end(), { "-a", "enable_lsmash=true" });
#endif
#ifdef __linux__
	bool bundled = std::filesystem::exists(blur.resources_path / "vapoursynth-plugins");
	args.insert(args.end(), { "-a", std::format("linux_bundled={}", bundled ? "true" : "false") });
#endif

	args.insert(args.end(), { u::path_to_string(blur.resources_path / "lib/blur.py"), "-" });
	return args;
}

std::vector<std::string> rendering::detail::build_vspipe_video_args(
	const std::filesystem::path& input_path,
	const nlohmann::json& merged_settings,
	const u::VideoInfo& video_info,
	size_t start_frame,
	size_t end_frame
) {
	auto args = build_vspipe_base_args(input_path, merged_settings);
	args.insert(
		args.end() - 2,
		{
			"-a",
			std::format("fps_num={}", video_info.fps_num),
			"-a",
			std::format("fps_den={}", video_info.fps_den),
			"-a",
			"color_range=" + (video_info.color_range ? *video_info.color_range : "undefined"),
			"-a",
			std::format("start={}", start_frame),
			"-a",
			std::format("end={}", end_frame),
		}
	);
	return args;
}

tl::expected<std::filesystem::path, std::string> rendering::detail::build_output_filename(
	const std::filesystem::path& input_path, const BlurSettings& settings, const GlobalAppSettings& app_settings
) {
	auto output_folder = (input_path.parent_path() / app_settings.output_prefix).lexically_normal();

	try {
		std::filesystem::create_directories(output_folder);
	}
	catch (const std::filesystem::filesystem_error& e) {
		return tl::unexpected(fmt::format("Failed to create output directory: {}", e.what()));
	}

	std::string base_name = std::format("{} - blur", input_path.stem());

	if (settings.detailed_filenames) {
		std::string details;
		if (settings.blur && settings.interpolate) {
			details = std::format(
				"{}fps ({}, {})", settings.blur_output_fps, settings.interpolated_fps, settings.blur_amount
			);
		}
		else if (settings.blur) {
			details = std::format("{}fps ({})", settings.blur_output_fps, settings.blur_amount);
		}
		else if (settings.interpolate) {
			details = std::format("{}fps", settings.interpolated_fps);
		}

		if (!details.empty())
			base_name += " ~ " + details;
	}

	// find unique filename
	int counter = 1;
	std::filesystem::path result;
	do {
		std::string filename = base_name;
		if (counter > 1)
			filename += std::format(" ({})", counter);
		filename += "." + settings.advanced.video_container;
		result = output_folder / filename;
		counter++;
	}
	while (std::filesystem::exists(result));

	return result;
}

std::vector<std::string> rendering::detail::build_ffmpeg_video_args(
	const std::filesystem::path& input_path,
	const u::VideoInfo& video_info,
	const BlurSettings& settings,
	const GlobalAppSettings& app_settings,
	const std::filesystem::path& output_path,
	size_t start_frame,
	size_t end_frame
) {
	std::vector<std::string> args = {
		"-loglevel",
		"error",
		"-hide_banner",
		"-stats",
		"-y",
		"-fflags",
		"+genpts",
		"-i",
		"-",
		"-i",
		u::path_to_string(input_path),
		"-map",
		"0:v",
	};

	append_audio_filter_args(args, video_info, settings, start_frame, end_frame);
	append_colour_param_args(args, video_info);
	append_encoding_args(args, settings, app_settings);

	args.push_back(u::path_to_string(output_path));
	return args;
}

void rendering::detail::copy_file_timestamp(const std::filesystem::path& from, const std::filesystem::path& to) {
	try {
		auto timestamp = std::filesystem::last_write_time(from);
		std::filesystem::last_write_time(to, timestamp);
	}
	catch (const std::exception& e) {
		u::log_error("Failed to copy timestamp: {}", e.what());
	}
}
