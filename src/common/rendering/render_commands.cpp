#include "render_commands.h"
#include "output.h"
#include "common/vspipe.h"
#include "common/plugins.h"

namespace {
	// -copyts keeps the source on ffprobe's clock, otherwise ffmpeg moves an mpegts audio stream to 0 when its video
	// isn't mapped
	void append_input_args(
		std::vector<std::string>& args, const std::filesystem::path& input_path, double source_offset = 0.0
	) {
		args.insert(args.end(), { "-copyts", "-fflags", "+genpts", "-i", "-" });

		if (source_offset != 0.0)
			args.insert(args.end(), { "-itsoffset", std::format("{:.6f}", source_offset) });

		args.insert(args.end(), { "-i", u::path_to_string(input_path), "-map", "0:v" });
	}

	void append_audio_filter_args(
		std::vector<std::string>& args,
		const media::VideoInfo& video_info,
		const BlurSettings& settings,
		rendering::detail::FrameRange range,
		size_t skipped_frames
	) {
		if (video_info.audio_sample_rates.empty())
			return;

		float speed = settings.timescale ? settings.output_timescale / settings.input_timescale : 1.f;

		double skipped_time = static_cast<double>(skipped_frames) / settings.blur_output_fps * speed;
		double frame_duration = static_cast<double>(video_info.fps_den) / video_info.fps_num;

		double start_time = video_info.video_start_time + (range.start * frame_duration) + skipped_time;
		double end_time = video_info.video_start_time + (range.end * frame_duration);

		std::string complex_filter;
		for (size_t i = 0; i < video_info.audio_sample_rates.size(); i++) {
			if (i > 0)
				complex_filter += ";";

			int sample_rate = video_info.audio_sample_rates[i];

			std::string timescale_filter;
			double lead = 0.0;
			if (speed != 1.f && settings.output_timescale_audio_pitch) {
				int shifted_rate = static_cast<int>(std::round(sample_rate * speed));
				timescale_filter = std::format(",asetrate={},aresample={}", shifted_rate, sample_rate);
			}
			else if (speed != 1.f) {
				// atempo starts half a window early, so it's given audio from that much sooner
				double half_window = std::bit_ceil(static_cast<unsigned>(sample_rate / 24)) / 2.0;
				double input_per_sample = 1.0;

				// atempo only goes down to 0.5 and skips samples past 2, so bigger changes are chained
				auto add_atempo = [&](float tempo) {
					timescale_filter += std::format(",atempo={:.6f}", tempo);
					lead += half_window * input_per_sample;
					input_per_sample *= tempo;
				};

				float s = std::clamp(speed, 0.25f, 100.f);
				while (s > 2.f) {
					add_atempo(2.f);
					s /= 2.f;
				}
				while (s < 0.5f) {
					add_atempo(0.5f);
					s /= 0.5f;
				}
				add_atempo(s);
			}

			int64_t start_sample = std::llround((start_time * sample_rate) - lead);
			int64_t end_sample = std::llround(end_time * sample_rate);

			// audio starting after the video is padded with silence rather than moved to the start
			complex_filter += std::format(
				"[1:a:{}]atrim=start_pts={}:end_pts={},asetpts=PTS-({}),aresample=first_pts=0{}[a{}]",
				i,
				start_sample,
				end_sample,
				start_sample,
				timescale_filter,
				i
			);
		}

		args.insert(args.end(), { "-filter_complex", complex_filter });

		for (size_t i = 0; i < video_info.audio_sample_rates.size(); i++) {
			args.insert(args.end(), { "-map", std::format("[a{}]", i) });
		}
	}

	bool is_rgb_pix_fmt(const std::string& pix_fmt) {
		return pix_fmt.find("rgb") != std::string::npos || pix_fmt.find("bgr") != std::string::npos ||
		       pix_fmt.starts_with("gbr");
	}

	// vspipe's y4m doesn't carry the source's colour metadata, so it's set again before anything reads it
	std::optional<std::string> build_colour_param_filter(const media::VideoInfo& video_info) {
		std::vector<std::string> params;

		// blur.py converts rgb sources to yuv, so their matrix doesn't carry over
		bool rgb = video_info.pix_fmt && is_rgb_pix_fmt(*video_info.pix_fmt);

		if (video_info.color_range && *video_info.color_range != "") {
			std::string range = *video_info.color_range == "pc" ? "full" : "limited";
			params.emplace_back("range=" + range);
		}

		if (!rgb && video_info.color_space && *video_info.color_space != "")
			params.emplace_back("colorspace=" + *video_info.color_space);

		if (video_info.color_transfer && *video_info.color_transfer != "")
			params.emplace_back("color_trc=" + *video_info.color_transfer);

		if (video_info.color_primaries && *video_info.color_primaries != "")
			params.emplace_back("color_primaries=" + *video_info.color_primaries);

		if (params.empty())
			return {};

		return "setparams=" +
		       std::accumulate(
				   std::next(params.begin()), params.end(), params[0], [](const std::string& a, const std::string& b) {
					   return a + ":" + b;
				   }
			   );
	}

	void append_colour_param_args(std::vector<std::string>& args, const media::VideoInfo& video_info) {
		if (auto filter = build_colour_param_filter(video_info))
			args.insert(args.end(), { "-vf", *filter });

		// blur.py converts rgb sources to yuv, so their pixel format doesn't carry over
		bool rgb = video_info.pix_fmt && is_rgb_pix_fmt(*video_info.pix_fmt);

		if (video_info.pix_fmt && !rgb) {
			args.insert(args.end(), { "-pix_fmt", *video_info.pix_fmt });
		}
	}

	// jpegs are full range bt601, which ffmpeg won't convert to unless it's told to
	constexpr std::string_view JPEG_COLOUR = "out_color_matrix=bt601:out_range=pc";

	constexpr int PREVIEW_MAX_HEIGHT = 720;
}

nlohmann::json rendering::detail::merge_settings(
	const BlurSettings& blur_settings,
	const GlobalAppSettings& app_settings,
	const devices::DeviceIndices& device_indices
) {
	auto settings_json = blur_settings.to_json();
	settings_json.update(app_settings.to_json());
	settings_json["rife_device_index"] = device_indices.rife;
	settings_json["tensorrt_device_index"] = device_indices.tensorrt;

	std::optional<std::filesystem::path> lossless_dll;
	if (blur_settings.uses_interpolation_method("lsfg"))
		lossless_dll = plugins::lsfg::find_dll(app_settings.lossless_scaling_path);
	settings_json["lsfg_dll"] = lossless_dll ? u::path_to_string(*lossless_dll) : "";

	return settings_json;
}

std::vector<std::string> rendering::detail::build_vspipe_base_args(
	const std::filesystem::path& input_path, const nlohmann::json& merged_settings
) {
	std::string path_str = u::path_to_string(input_path);
	std::ranges::replace(path_str, '\\', '/');

	return vspipe::get_args(
		{ "-p", "-c", "y4m" },
		"blur.py",
		{
			"video_path=" + path_str,
			"settings=" + merged_settings.dump(),
			"settings_path=" + u::path_to_string(blur.settings_path),
		},
		"-"
	);
}

std::vector<std::string> rendering::detail::build_vspipe_video_args(
	const std::filesystem::path& input_path,
	const nlohmann::json& merged_settings,
	const media::VideoInfo& video_info,
	const VspipeVideoOptions& options
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
			std::format("preroll_frames={}", video_info.preroll_frames),
		}
	);

	if (options.range) {
		args.insert(args.end(), { "-a", std::format("start={}", options.range->start) });
		args.insert(args.end(), { "-a", std::format("end={}", options.range->end) });
	}

	if (options.mask_range) {
		args.insert(args.end(), { "-a", std::format("mask_start={}", options.mask_range->start) });
		args.insert(args.end(), { "-a", std::format("mask_end={}", options.mask_range->end) });
	}

	if (options.preview_mask)
		args.insert(args.end(), { "-a", "preview_mask=true" });

	if (video_info.frameserver)
		args.insert(args.end(), { "-a", "frameserver=true" });

	if (video_info.video_stream_index >= 0)
		args.insert(args.end(), { "-a", std::format("video_stream_index={}", video_info.video_stream_index) });

	if (video_info.lsmas_issue)
		args.insert(args.end(), { "-a", "lsmas_issue=" + *video_info.lsmas_issue });

	if (options.skipped_frames > 0)
		args.insert(args.end(), { "-a", std::format("skip_frames={}", options.skipped_frames) });

	return args;
}

tl::expected<std::vector<std::string>, std::string> rendering::detail::build_ffmpeg_video_args(
	const std::filesystem::path& input_path,
	const media::VideoInfo& video_info,
	const BlurSettings& settings,
	const GlobalAppSettings& app_settings,
	const std::filesystem::path& output_path,
	FrameRange range,
	bool trimming
) {
	auto encoding_args = build_encoding_args(settings, app_settings, nullptr);

	bool copy_audio = !video_info.audio_sample_rates.empty() && wants_audio_copy(encoding_args);

	if (copy_audio) {
		if (auto conflict = get_audio_copy_conflict(settings, trimming))
			return tl::unexpected(*conflict);
	}

	std::vector<std::string> args = { "-loglevel", "error", "-hide_banner", "-stats", "-y" };

	// copied audio can't be trimmed, but it can be moved to where the video starts
	append_input_args(args, input_path, copy_audio ? -video_info.video_start_time : 0.0);

	if (copy_audio) {
		for (size_t i = 0; i < video_info.audio_sample_rates.size(); i++) {
			args.insert(args.end(), { "-map", std::format("1:a:{}", i) });
		}
	}
	else {
		append_audio_filter_args(
			args, video_info, settings, range, get_skipped_frames(settings, app_settings, video_info)
		);
	}

	append_colour_param_args(args, video_info);

	// append encoding args at end
	args.insert(args.end(), encoding_args.begin(), encoding_args.end());

	args.push_back(u::path_to_string(output_path));
	return args;
}

tl::expected<std::vector<std::string>, std::string> rendering::detail::build_ffmpeg_pre_render_args(
	const std::filesystem::path& input_path,
	const media::VideoInfo& video_info,
	const BlurSettings& settings,
	const GlobalAppSettings& app_settings,
	const std::filesystem::path& output_path,
	FrameRange range,
	size_t skipped_frames
) {
	std::vector<std::string> encoding_args = { "-c:v", "libx264", "-preset", "ultrafast", "-crf", "16" };

	if (app_settings.pre_render_output_encoding) {
		auto output_args = build_output_encoding_args(video_info, settings, app_settings);
		if (!output_args)
			return tl::unexpected(output_args.error());

		encoding_args = std::move(*output_args);
	}

	std::vector<std::string> args = { "-loglevel", "error", "-hide_banner", "-y" };
	append_input_args(args, input_path);

	if (video_info.ffmpeg_can_decode_audio)
		append_audio_filter_args(args, video_info, settings, range, skipped_frames);

	append_colour_param_args(args, video_info);

	// it can be finished early, which cuts the video short of the audio
	args.push_back("-shortest");

	args.insert(args.end(), encoding_args.begin(), encoding_args.end());

	// the pre-render's audio is trimmed, so it can't be copied
	if (!app_settings.pre_render_output_encoding || wants_audio_copy(encoding_args))
		args.insert(args.end(), { "-c:a", "aac" });

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

std::vector<std::string> rendering::detail::build_ffmpeg_preview_args(const media::VideoInfo& video_info) {
	std::string filter = std::format("scale=-2:min(ih\\,{}):{}", PREVIEW_MAX_HEIGHT, JPEG_COLOUR);

	// without it full range sources are read as limited and stretched
	if (auto colour = build_colour_param_filter(video_info))
		filter = *colour + "," + filter;

	return {
		"-vf", filter, "-c:v", "mjpeg", "-q:v", "2", "-f", "image2pipe", "-",
	};
}
