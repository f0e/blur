#include "render.h"
#include "render_commands.h"
#include "render_pipeline.h"

namespace {
	constexpr int FRAMES_NEEDED_FOR_VSPIPE_TO_NOT_POO_ITSELF =
		3; // need some buffer of frames so something actually renders out. idk how to get a proper value for this.
	       // @todo: make sure this doesnt ever fail (set config preview seek = 1 and try lots of diff vids and configs,
	       // make sure preview never fails to render)

	// render_frame skips this far into the blurred output - the first frame doesn't have anything to blend with yet.
	// @todo: revisit if i ever change that behaviour (first frame of video not being blurred properly thing)
	double get_output_seek(const BlurSettings& settings) {
		return 1.0 / settings.blur_output_fps;
	}

	size_t get_seek_start_frame(const BlurSettings& settings, const u::VideoInfo& video_info, float seek) {
		if (!video_info.has_video_stream || video_info.fps_num <= 0 || video_info.fps_den <= 0 ||
		    video_info.duration <= 0.f)
			return 0;

		double ffmpeg_required_extra_time = get_output_seek(settings);
		double fps = static_cast<double>(video_info.fps_num) / video_info.fps_den;

		size_t total_frames = static_cast<size_t>(video_info.duration * fps);

		size_t total_usable_frames = static_cast<size_t>(std::max(
			0.0, ((video_info.duration - ffmpeg_required_extra_time) * fps) - FRAMES_NEEDED_FOR_VSPIPE_TO_NOT_POO_ITSELF
		));

		size_t start_frame = static_cast<size_t>(std::clamp(seek, 0.f, 1.f) * total_frames);
		size_t offset = total_frames > total_usable_frames ? total_frames - total_usable_frames : 0;

		return offset < start_frame ? start_frame - offset : 0;
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

	auto video_info = u::get_video_info(input_path);

	auto vspipe_args = detail::build_vspipe_video_args(
		input_path, *merged_settings, video_info, get_seek_start_frame(settings, video_info, seek)
	);

	RenderCommands commands = {
        .vspipe_video = std::move(vspipe_args),
        .ffmpeg = {
            "-loglevel", "error",
            "-hide_banner",
            "-stats",
            "-i", "-",
            "-ss", std::to_string(get_output_seek(settings)),
            "-map", "0:v",
            "-frames:v", "1",
            "-c:v", "mjpeg",
            "-q:v", "2",
            "-f", "image2pipe",
            "-",
        },
        .ffmpeg_stops_early = true,
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

// get_seek_start_frame handles the rest of the unusable-video cases itself
float rendering::get_preview_frame_timestamp(const BlurSettings& settings, const u::VideoInfo& video_info, float seek) {
	if (video_info.fps_num <= 0 || video_info.fps_den <= 0)
		return 0.f;

	double fps = static_cast<double>(video_info.fps_num) / video_info.fps_den;

	return static_cast<float>((get_seek_start_frame(settings, video_info, seek) / fps) + get_output_seek(settings));
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
