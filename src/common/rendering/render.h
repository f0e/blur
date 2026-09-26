#pragma once

#include "render_types.h"
#include "render_errors.h"
#include "render_state.h"
#include "common/config_app.h"
#include "common/config_blur.h"
#include "common/media.h"

// turn settings into commands, run the pipeline, and handle the output file
namespace rendering {
	// a .vpy that runs blur.py with the globals vspipe would give it, so it can be opened in-process (e.g. by mpv).
	// what blur.py prints while the script's being evaluated, like status lines, goes to log_path
	tl::expected<std::string, std::string> build_preview_script(
		const std::filesystem::path& input_path,
		const BlurSettings& settings,
		const GlobalAppSettings& app_settings,
		const media::VideoInfo& video_info,
		bool preview_mask,
		const std::filesystem::path& log_path
	);

	std::pair<size_t, size_t> get_trim_frame_range(const media::VideoInfo& video_info, float start, float end);

	bool has_enough_frames_to_render(const media::VideoInfo& video_info, float start, float end);

	namespace detail {
		tl::expected<RenderResult, std::variant<std::string, RenderError>> render_video(
			const std::filesystem::path& input_path,
			const media::VideoInfo& video_info,
			const BlurSettings& settings,
			const std::shared_ptr<RenderState>& state,
			const GlobalAppSettings& app_settings,
			const std::optional<std::filesystem::path>& output_path_override,
			float start,
			float end,
			const std::function<void()>& progress_callback
		);
	}
}
