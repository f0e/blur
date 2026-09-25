#pragma once

#include "common/config_app.h"
#include "common/config_encoding_presets.h"
#include "common/config_rules.h"
#include "../../ui/ui.h"

namespace gui::components::configs { // naming it configs to avoid conflict with common lol
	inline const gfx::Color ERROR_COLOR(255, 0, 0, 255);
	inline const gfx::Color WARNING_COLOR(252, 186, 3, 150);

	const gfx::Color DELETE_ICON_COLOR = gfx::Color::white(120);
	const gfx::Color DEFAULT_CONFIG_ICON_COLOR = gfx::Color(255, 100, 100, 255);
	const gfx::Color DELETE_ICON_HOVER_COLOR(255, 80, 80, 255);
	const int DELETE_ICON_GAP = 6;
	const int SEEK_BAR_BOTTOM_GAP = 9;

	inline const std::vector<std::string> CONFIG_TABS = { "blur", "app", "encoding" };
	inline std::string selected_config_tab = CONFIG_TABS[0];
	inline constexpr int CONFIG_HEADER_NAV_GAP = 6;

	inline const std::vector<std::string> RIGHT_TABS = { "preview", "weightings", "rules" };
	inline std::string selected_right_tab = RIGHT_TABS[0];
	inline std::string hovered_weighting;
	inline std::string hovered_mask;
	inline std::string hovered_config;

	// dropdowns can switch the preview panel to their own tab while they're open
	inline std::string old_tab;
	inline std::string temp_tab_owner;

	void set_temporary_tab(const std::string& owner, bool open, const std::string& tab);

	void release_stale_temporary_tab();

	inline const std::string CONFIG_DROPDOWN_ID = "blur config dropdown";

	// the selected config, flushed back into edited_configs with flush_selected_config
	inline BlurSettings settings;
	inline std::string selected_config_name;

	inline std::map<std::string, BlurSettings> edited_configs;
	inline std::map<std::string, BlurSettings> saved_configs;

	inline GlobalAppSettings app_settings;
	inline GlobalAppSettings current_app_settings;

	inline EncodingPresetSettings encoding_preset_settings;
	inline EncodingPresetSettings current_encoding_preset_settings;

	inline ConfigRuleSettings rule_settings;
	inline ConfigRuleSettings current_rule_settings;
	inline std::string selected_encoding_preset_gpu_type;

	inline bool show_mask_preview = false;

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
	void encoding_preset_options(ui::Container& container);

	void config_management(ui::Container& container);

	void flush_selected_config();

	void select_config(const std::string& name);

	void about(ui::Container& container);

	bool theme_preview_open(const ui::Container& config_container);
	void theme_preview(ui::Container& container);

	void config_preview(ui::Container& container);
	void reset_config_preview();

	void preview_tabs(ui::Container& container);
	void preview(ui::Container& container, float delta_time);
	void rules(ui::Container& container, float delta_time);
	void option_information(ui::Container& container);

	// keeps a value in sync with the buffer an element edits
	std::string& bind_input(const std::string& id, std::string& value);
	std::string& bind_read_only_input(const std::string& id, const std::string& value);
	bool& bind_checkbox(const std::string& id, bool& value);

	void parse_interp();
	bool has_unsaved_changes();
	void enter_screen();

	void import_config(const BlurSettings& imported);

	void leave_screen(const std::function<void()>& on_leave);
	void save_config();
	void on_load();

	void screen(
		ui::Container& container,
		ui::Container& nav_container,
		ui::Container& preview_content_container,
		ui::Container& option_information_container,
		float delta_time
	);
}
