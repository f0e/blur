#pragma once

#include "common/config_app.h"
#include "common/config_blur.h"
#include "common/rendering/render_state.h"
#include "../../render/render.h"

class VideoPlayer;

namespace gui::components::configs::preview_frames {
	struct Request {
		// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members) only lives for the ui frame it's built in
		const std::filesystem::path& video_path;
		const BlurSettings& settings;
		const GlobalAppSettings& app_settings;
		// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
		bool seeking = false;
		bool show_mask = false;
	};

	struct Frame {
		std::shared_ptr<render::Texture> texture;
		std::string image_id;
		std::shared_ptr<VideoPlayer> player;
		bool up_to_date = false;
	};

	struct Result {
		std::optional<Frame> frame;
		bool rendering = false;
		float video_duration = 0.f;
		rendering::RenderState::InitStage init_stage = rendering::RenderState::InitStage::NONE;
		std::string frame_timing_log;
	};

	Result update(const Request& request);

	void handle_event(const SDL_Event& event, bool& to_render);

	std::vector<uint8_t> current_mask_jpeg();

	void reset();
}
