#pragma once

#include "render_types.h"
#include "render_state.h"
#include "common/config_app.h"
#include "common/config_blur.h"

// Top-level render orchestration: turn settings into commands, run the
// pipeline, and deal with the output file.
namespace rendering {
	struct FrameRenderResult {
		std::vector<uint8_t> frame_jpeg;
		bool stopped = false;
	};

	tl::expected<FrameRenderResult, std::variant<std::string, RenderError>> render_frame(
		const std::filesystem::path& input_path,
		const BlurSettings& settings,
		const GlobalAppSettings& app_settings = config_app::get_app_config(),
		const std::shared_ptr<RenderState>& state = std::make_shared<RenderState>(),
		float seek = 0.f
	);

	namespace detail {
		tl::expected<RenderResult, std::variant<std::string, RenderError>> render_video(
			const std::filesystem::path& input_path,
			const u::VideoInfo& video_info,
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
