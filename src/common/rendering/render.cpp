#include "render.h"
#include "render_commands.h"
#include "render_pipeline.h"

namespace {
	size_t get_seek_start_frame(const std::filesystem::path& input_path, float seek) {
		auto video_info = u::get_video_info(input_path);
		if (!video_info.has_video_stream || video_info.fps_num <= 0 || video_info.fps_den <= 0 ||
		    video_info.duration <= 0.f)
			return 0;

		double fps = static_cast<double>(video_info.fps_num) / video_info.fps_den;
		auto total_frames = static_cast<size_t>(video_info.duration * fps);

		return std::min(
			static_cast<size_t>(std::clamp(seek, 0.f, 1.f) * total_frames), total_frames - 1
		); // cap at total_frames - 1 cause otherwise start will be end and vspipe will error (needs at least 1 frame)
	}
}

tl::expected<rendering::FrameRenderResult, std::variant<std::string, rendering::RenderError>> rendering::render_frame(
	const std::filesystem::path& input_path,
	const BlurSettings& settings,
	const GlobalAppSettings& app_settings,
	const std::shared_ptr<RenderState>& state,
	float seek
) {
	if (!blur.initialised)
		return tl::unexpected("Blur not initialised");

	if (!std::filesystem::exists(input_path))
		return tl::unexpected("Input path does not exist");

	auto merged_settings = detail::merge_settings(settings, app_settings);
	if (!merged_settings)
		return tl::unexpected(merged_settings.error());

	auto vspipe_args = detail::build_vspipe_base_args(input_path, *merged_settings);

	if (seek > 0.f) {
		size_t start_frame = get_seek_start_frame(input_path, seek);
		if (start_frame > 0) {
			vspipe_args.insert(vspipe_args.end() - 2, { "-a", std::format("start={}", start_frame) });
		}
	}

	RenderCommands commands = {
        .vspipe_video = std::move(vspipe_args),
        .ffmpeg = {
            "-loglevel", "error",
            "-hide_banner",
            "-stats",
            "-i", "-",
            "-map", "0:v",
            "-vframes", "1",
            "-q:v", "2",
            "-f", "image2pipe",
            "-vcodec", "mjpeg",
            "-",
        },
    };

	state->enable_preview_capture();

	auto pipeline_result = detail::execute_pipeline(commands, state, settings.advanced.debug, false, nullptr);

	if (!pipeline_result)
		return tl::unexpected(pipeline_result.error());

	return FrameRenderResult{
		.frame_jpeg = state->take_preview_jpeg(),
		.stopped = pipeline_result->stopped,
	};
}

tl::expected<rendering::RenderResult, std::variant<std::string, rendering::RenderError>> rendering::detail::
	render_video(
		const std::filesystem::path& input_path,
		const u::VideoInfo& video_info,
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

	auto merged_settings = detail::merge_settings(settings, app_settings);
	if (!merged_settings)
		return tl::unexpected(merged_settings.error());

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

	// compute cut points
	double abs_start_time = video_info.video_start_time + (start * video_info.duration);
	double abs_end_time = video_info.video_start_time + (end * video_info.duration);

	auto start_frame = static_cast<size_t>(
		((abs_start_time - video_info.video_start_time) * video_info.fps_num / video_info.fps_den) + 0.5
	);
	auto end_frame = static_cast<size_t>(
		((abs_end_time - video_info.video_start_time) * video_info.fps_num / video_info.fps_den) + 0.5
	);

	auto ffmpeg_args = detail::build_ffmpeg_video_args(
		input_path, video_info, settings, app_settings, output_path, start_frame, end_frame, start != 0.f || end != 1.f
	);
	if (!ffmpeg_args)
		return tl::unexpected(ffmpeg_args.error());

	RenderCommands commands = {
		.vspipe_video =
			detail::build_vspipe_video_args(input_path, *merged_settings, video_info, start_frame, end_frame),
		.ffmpeg = *ffmpeg_args,
	};

	// add preview pipe if needed
	if (settings.preview && blur.using_preview) {
		commands.ffmpeg.insert(
			commands.ffmpeg.end(),
			{
				"-map",
				"0:v",
				"-q:v",
				"2",
				"-update",
				"1",
				"-f",
				"image2pipe",
				"-vcodec",
				"mjpeg",
				"-",
			}
		);

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
