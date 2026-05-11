#pragma once

#include "common/config_app.h"
#include "common/rendering.h"
#include "../../ui/ui.h"

namespace gui::components::configs { // naming it configs to avoid conflict with common lol
	inline const std::vector<std::string> TABS = { "output video", "weightings" };
	inline std::string selected_tab = TABS[0];
	inline std::string old_tab;
	inline std::string hovered_weighting;

	inline BlurSettings settings;
	inline BlurSettings current_global_settings;

	inline GlobalAppSettings app_settings;
	inline GlobalAppSettings current_app_settings;

	inline bool loaded_config = false;
	inline bool loading_config = false;

	inline bool interpolate_scale = true;
	inline float interpolated_fps_mult = 5.f;
	inline int interpolated_fps = 1200;

	inline bool pre_interpolate_scale = true;
	inline float pre_interpolated_fps_mult = 2.f;
	inline int pre_interpolated_fps = 360;

	struct PreviewRenderState {
		bool can_delete = false;
		std::shared_ptr<rendering::RenderState> state = std::make_shared<rendering::RenderState>();
	};

	inline std::vector<std::shared_ptr<PreviewRenderState>> render_states;
	inline std::mutex render_mutex;

	inline bool just_added_sample_video = false;

	void set_interpolated_fps();
	void set_pre_interpolated_fps();

	void options(ui::Container& container);

	void config_preview(ui::Container& container);
	void reset_config_preview();

	void preview(ui::Container& header_container, ui::Container& content_container);
	void option_information(ui::Container& container);

	void parse_interp();
	void save_config();
	void on_load();

	void screen(
		ui::Container& container,
		ui::Container& nav_container,
		ui::Container& preview_header_container,
		ui::Container& preview_content_container,
		ui::Container& option_information_container,
		float delta_time
	);
}
