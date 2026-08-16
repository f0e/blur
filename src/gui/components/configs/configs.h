#pragma once

#include "common/config_app.h"
#include "common/config_presets.h"
#include "common/rendering.h"
#include "../../ui/ui.h"

namespace gui::components::configs { // naming it configs to avoid conflict with common lol
	inline const gfx::Color ERROR_COLOR(255, 0, 0, 255);
	inline const gfx::Color WARNING_COLOR(252, 186, 3, 150);

	void add_with_message(
		ui::Container& container,
		const std::string& message_id,
		const std::optional<std::string>& message,
		const gfx::Color& color,
		const std::function<void()>& add_element
	);

	inline const std::vector<std::string> CONFIG_TABS = { "blur", "app", "presets" };
	inline std::string selected_config_tab = CONFIG_TABS[0];

	inline const std::vector<std::string> TABS = { "output video", "weightings" };
	inline std::string selected_tab = TABS[0];
	inline std::string old_tab;
	inline std::string hovered_weighting;

	inline BlurSettings settings;
	inline BlurSettings current_global_settings;

	inline GlobalAppSettings app_settings;
	inline GlobalAppSettings current_app_settings;

	inline PresetSettings preset_settings;
	inline PresetSettings current_preset_settings;
	inline std::string selected_preset_gpu_type; // which device's presets the presets tab is showing

	inline bool loaded_config = false;
	inline bool should_load_config = true;

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

	void section(
		ui::Container& container,
		bool& first_section,
		const std::string& label,
		bool* setting = nullptr,
		bool forced_on = false
	);

	void options(ui::Container& container);
	void app_options(ui::Container& container);
	void preset_options(ui::Container& container);

	// why the preset at this index can't be saved, if it can't
	std::optional<std::string> get_preset_error(const std::vector<PresetSettings::Preset>& presets, size_t index);

	// the device holding the first preset that can't be saved, if there is one
	std::optional<std::string> find_preset_error_device();

	std::optional<std::string> get_settings_error();

	void about(ui::Container& container);

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
