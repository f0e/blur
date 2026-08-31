#pragma once

#include "common/config_app.h"
#include "common/config_presets.h"
#include "common/rendering.h"
#include "../../fonts/icons.h"
#include "../../ui/ui.h"

namespace gui::components::configs { // naming it configs to avoid conflict with common lol
	inline const gfx::Color ERROR_COLOR(255, 0, 0, 255);
	inline const gfx::Color WARNING_COLOR(252, 186, 3, 150);

	const gfx::Color DELETE_ICON_COLOR = gfx::Color::white(120);
	const gfx::Color DELETE_ICON_HOVER_COLOR(255, 80, 80, 255);
	const int DELETE_ICON_GAP = 6;
	const int SEEK_BAR_BOTTOM_GAP = 9;

	void add_with_message(
		ui::Container& container,
		const std::string& message_id,
		const std::optional<std::string>& message,
		const gfx::Color& color,
		const std::function<void()>& add_element
	);

	inline const std::vector<std::string> CONFIG_TABS = { "blur", "app", "presets" };
	inline std::string selected_config_tab = CONFIG_TABS[0];
	inline constexpr int CONFIG_HEADER_NAV_GAP = 6;

	inline const std::vector<std::string> TABS = { "preview", "weightings" };
	inline std::string selected_tab = TABS[0];
	inline std::string old_tab;
	inline std::string hovered_weighting;
	inline std::string hovered_mask;

	inline BlurSettings settings;
	inline BlurSettings current_global_settings;

	inline GlobalAppSettings app_settings;
	inline GlobalAppSettings current_app_settings;

	inline PresetSettings preset_settings;
	inline PresetSettings current_preset_settings;
	inline std::string selected_preset_gpu_type; // which device's presets the presets tab is showing

	inline bool show_mask_preview = false;

	inline bool loaded_config = false;
	inline bool should_load_config = true;

	inline bool interpolate_scale = true;
	inline float interpolated_fps_mult = 5.f;
	inline int interpolated_fps = 1200;

	inline bool pre_interpolate_scale = true;
	inline float pre_interpolated_fps_mult = 2.f;
	inline int pre_interpolated_fps = 360;

	bool has_sample_video();
	void set_sample_video(const std::filesystem::path& path);
	void clear_sample_video();
	void save_preview_app_settings();

	void set_interpolated_fps();
	void set_pre_interpolated_fps();

	void section(
		ui::Container& container,
		bool& first_section,
		const std::string& label,
		bool* setting = nullptr,
		bool forced_on = false
	);

	void config_actions(ui::Container& container);

	void options(ui::Container& container);
	void app_options(ui::Container& container);
	void preset_options(ui::Container& container);

	void about(ui::Container& container);

	bool theme_preview_open(const ui::Container& config_container);
	void theme_preview(ui::Container& container);

	void config_preview(ui::Container& container);
	void reset_config_preview();

	void preview_tabs(ui::Container& header_container, ui::Container& content_container);
	void preview(ui::Container& header_container, ui::Container& content_container);
	void option_information(ui::Container& container);

	void parse_interp();
	bool has_unsaved_changes();
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
