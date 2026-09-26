#include "render.h"
#include "render_commands.h"
#include "render_pipeline.h"
#include "common/devices.h"
#ifdef TENSORRT
#	include "common/rife_models.h"
#endif
#include "common/media.h"

namespace {
	std::optional<std::string> check_tensorrt_installed(const BlurSettings& settings) {
#ifdef TENSORRT
		if (!settings.uses_interpolation_method("rife (tensorrt)"))
			return {};

		if (!rife_models::trt_installed()) {
			return "TensorRT RIFE isn't installed. Rerun the installer and select \"NVIDIA TensorRT RIFE interpolation\", or use a different interpolation method";
		}

		std::error_code ec;
		if (!std::filesystem::exists(rife_models::get_trt_path() / (settings.rife_trt_model + ".onnx"), ec)) {
			return std::format(
				"TensorRT RIFE model '{}' wasn't found in {}",
				settings.rife_trt_model,
				u::path_to_string(rife_models::get_trt_path())
			);
		}
#endif

		return {};
	}
}

tl::expected<std::string, std::string> rendering::build_preview_script(
	const std::filesystem::path& input_path,
	const BlurSettings& settings,
	const GlobalAppSettings& app_settings,
	const media::VideoInfo& video_info,
	bool preview_mask,
	const std::filesystem::path& log_path
) {
	if (auto error = check_tensorrt_installed(settings))
		return tl::unexpected(*error);

	auto merged_settings = detail::merge_settings(settings, app_settings, devices::get_device_indices(app_settings));

	auto vspipe_args =
		detail::build_vspipe_video_args(input_path, merged_settings, video_info, {}, {}, {}, preview_mask);

	// vspipe hands every -a over as a string, so these stay strings too
	nlohmann::json script_args = nlohmann::json::object();
	for (size_t i = 0; i + 1 < vspipe_args.size(); i++) {
		if (vspipe_args[i] != "-a")
			continue;

		const auto& arg = vspipe_args[++i];
		auto split = arg.find('=');
		if (split != std::string::npos)
			script_args[arg.substr(0, split)] = arg.substr(split + 1);
	}

	auto python_path = [](const std::filesystem::path& path) {
		std::string str = u::path_to_string(path);
		std::ranges::replace(str, '\\', '/');
		return nlohmann::json(str).dump();
	};

	// vapoursynth reads VAPOURSYNTH_EXTRA_PLUGIN_PATH through its crt's cached environment, which setting it from the
	// app doesn't reliably reach, so the script loads the plugins itself
	std::string plugins_path = R"("")";
#ifdef _WIN32
	if (blur.used_installer)
		plugins_path = python_path(blur.resources_path / "lib/vapoursynth/vs-plugins");
#endif

	// json strings and objects of strings are valid python literals
	return std::format(
		R"(import sys

# python stays loaded between scripts, so this is only added once
lib_path = {}
if lib_path not in sys.path:
    sys.path.insert(1, lib_path)

import blur.preview

blur.preview.run({}, {}, {}, {})
)",
		python_path(blur.resources_path / "lib"),
		python_path(blur.resources_path / "lib" / "blur.py"),
		script_args.dump(),
		plugins_path,
		python_path(log_path)
	);
}

std::pair<size_t, size_t> rendering::get_trim_frame_range(const media::VideoInfo& video_info, float start, float end) {
	if (video_info.fps_num <= 0 || video_info.fps_den <= 0)
		return { 0, 0 };

	double abs_start_time = video_info.video_start_time + (start * video_info.duration);
	double abs_end_time = video_info.video_start_time + (end * video_info.duration);

	auto start_frame = static_cast<size_t>(
		std::llround((abs_start_time - video_info.video_start_time) * video_info.fps_num / video_info.fps_den)
	);
	auto end_frame = static_cast<size_t>(
		std::llround((abs_end_time - video_info.video_start_time) * video_info.fps_num / video_info.fps_den)
	);

	return { start_frame, end_frame };
}

bool rendering::has_enough_frames_to_render(const media::VideoInfo& video_info, float start, float end) {
	auto [start_frame, end_frame] = get_trim_frame_range(video_info, start, end);
	return end_frame > start_frame;
}

tl::expected<rendering::RenderResult, std::variant<std::string, rendering::RenderError>> rendering::detail::
	render_video(
		const std::filesystem::path& input_path,
		const media::VideoInfo& video_info,
		const BlurSettings& settings,
		const std::shared_ptr<RenderState>& state,
		const GlobalAppSettings& app_settings,
		const std::optional<std::filesystem::path>& output_path_override,
		float start,
		float end,
		const std::function<void()>& progress_callback
	) {
	if (!blur.initialised)
		return tl::unexpected("Blur not initialised");

	if (!std::filesystem::exists(input_path))
		return tl::unexpected("Input path does not exist");

	if (auto error = check_tensorrt_installed(settings))
		return tl::unexpected(*error);

	auto merged_settings = detail::merge_settings(settings, app_settings, devices::get_device_indices(app_settings));

	std::filesystem::path output_path;
	if (output_path_override) {
		output_path = *output_path_override;
	}
	else {
		auto output_res = detail::build_output_filename(input_path, settings, app_settings);
		if (!output_res) {
			return tl::unexpected(output_res.error());
		}

		output_path = *output_res;
	}

	u::log("Rendering '{}'", input_path.stem());

	if (blur.verbose) {
		u::log("Source video at {:.2f} timescale", settings.input_timescale);
		if (settings.interpolate) {
			u::log("Interpolated to {}fps with {:.2f} timescale", settings.interpolated_fps, settings.output_timescale);
		}
		if (settings.blur) {
			u::log(
				"Motion blurred to {}fps ({}%)", settings.blur_output_fps, static_cast<int>(settings.blur_amount * 100)
			);
		}
		u::log("Rendered at {:.2f} speed with crf {}", settings.output_timescale, settings.quality);
	}

	auto [start_frame, end_frame] = get_trim_frame_range(video_info, start, end);

	bool trimmed = start != 0.f || end != 1.f;

	if (video_info.frameserver && !video_info.ffmpeg_can_decode_audio) {
		return tl::unexpected(
			"Frameserver audio can't be read. Turn on 'Write audio as PCM samples in signpost AVI' in DebugMode "
			"FrameServer and restart it"
		);
	}

	auto ffmpeg_args = detail::build_ffmpeg_video_args(
		input_path, video_info, settings, app_settings, output_path, start_frame, end_frame, trimmed
	);
	if (!ffmpeg_args)
		return tl::unexpected(ffmpeg_args.error());

	RenderCommands commands = {
		.vspipe_video = detail::build_vspipe_video_args(
			input_path,
			merged_settings,
			video_info,
			start_frame,
			end_frame,
			// untrimmed renders use the whole video like previews do, so they share a cached mask
			trimmed ? std::optional{ std::pair{ start_frame, end_frame } } : std::nullopt,
			false,
			detail::get_skipped_frames(settings, app_settings, video_info)
		),
		.ffmpeg = *ffmpeg_args,
	};

	// add preview pipe if needed
	if (settings.preview && blur.using_preview) {
		auto preview_args = detail::build_ffmpeg_preview_args();
		commands.ffmpeg.insert(commands.ffmpeg.end(), preview_args.begin(), preview_args.end());

		state->enable_preview_capture();
	}

	auto pipeline_result = detail::execute_pipeline(commands, state, settings.advanced.debug, true, progress_callback);
	if (!pipeline_result)
		return tl::unexpected(pipeline_result.error());

	if (pipeline_result->stopped) {
		std::filesystem::remove(output_path);
		u::log("Stopped render '{}'", input_path.stem());
	}
	else {
		if (settings.copy_dates) {
			detail::copy_file_timestamp(input_path, output_path);
		}
		if (blur.verbose) {
			u::log("Finished rendering '{}'", input_path.stem());
		}
	}

	return RenderResult{
		.output_path = output_path,
		.stopped = pipeline_result->stopped,
	};
}
