#include "configs.h"
#include "../../renderer.h"

#include "../../ui/ui.h"
#include "../../render/render.h"
#include "../notifications.h"

namespace configs = gui::components::configs;

void configs::add_with_message(
	ui::Container& container,
	const std::string& message_id,
	const std::optional<std::string>& message,
	const gfx::Color& color,
	const std::function<void()>& add_element
) {
	if (message)
		container.push_element_gap(2);

	add_element();

	if (message) {
		container.pop_element_gap();

		ui::add_text(message_id, container, *message, color, fonts::dejavu);
	}
}

void configs::section(
	ui::Container& container, bool& first_section, const std::string& label, bool* setting, bool forced_on
) {
	if (!first_section) {
		ui::add_separator(std::format("section {} separator", label), container, ui::SeparatorStyle::FADE_RIGHT);
	}
	else
		first_section = false;

	if (!setting)
		return;

	if (!forced_on) {
		ui::add_checkbox(std::format("section {} checkbox", label), container, label, *setting, fonts::dejavu);
	}
	else {
		ui::add_text(std::format("section {}", label), container, label, gfx::Color::white(), fonts::dejavu);

		ui::add_text(
			std::format("section {} forced", label),
			container,
			"forced on as settings in this section have been modified",
			gfx::Color::white(renderer::MUTED_SHADE),
			fonts::dejavu
		);
	}
}

void configs::screen(
	ui::Container& config_container,
	ui::Container& nav_container,
	ui::Container& preview_header_container,
	ui::Container& preview_content_container,
	ui::Container& option_information_container,
	float delta_time
) {
	static bool loading_config = false;
	if (!loaded_config) {
		if (!loading_config) {
			loading_config = true;

			std::thread([] {
				ui::reset_tied_sliders();
				settings = config_blur::parse_global_config();
				app_settings = config_app::get_app_config();
				preset_settings = config_presets::get_preset_config();
				on_load();
				loading_config = false;
				loaded_config = true;
			}).detach();
		}

		ui::add_text(
			"config loading text",
			config_container,
			"Loading config...",
			gfx::Color::white(100),
			fonts::dejavu,
			FONT_CENTERED_X
		);

		ui::center_elements_in_container(config_container);
		return;
	}

	auto modified_default_app = config_app::DEFAULT_CONFIG;
	modified_default_app.gpu_type = app_settings.gpu_type;
	modified_default_app.rife_device_index =
		app_settings.rife_device_index; // the default config has uninitialised rife gpu, use index from current cfg to
	                                    // prevent restore default from always showing up
#ifdef TENSORRT
	modified_default_app.tensorrt_device_index = app_settings.tensorrt_device_index; // same for tensorrt
#endif

	bool config_changed = settings != current_global_settings || app_settings != current_app_settings ||
	                      preset_settings != current_preset_settings;
	bool config_not_default = settings != config_blur::DEFAULT_CONFIG || app_settings != modified_default_app ||
	                          preset_settings != config_presets::DEFAULT_CONFIG;

	if (config_changed) {
		ui::set_next_same_line(nav_container);
		ui::add_button("save button", nav_container, "Save", fonts::dejavu, [] {
			// saving would silently drop the broken presets, send the user to fix them instead
			if (auto preset_error = config_presets::validate(preset_settings)) {
				selected_config_tab = "presets";
				selected_preset_gpu_type = preset_error->gpu_type;

				gui::components::notifications::add(
					"preset errors", "Fix the errors in your presets before saving", ui::NotificationType::NOTIF_ERROR
				);

				return;
			}

			auto validation = config_blur::validate(settings, app_settings, preset_settings, false);
			if (!validation.ok()) {
				selected_config_tab = "blur";
				selected_tab = TABS[0];

				gui::components::notifications::add(
					"settings error", validation.message(), ui::NotificationType::NOTIF_ERROR
				);

				return;
			}

			save_config();
		});

		ui::set_next_same_line(nav_container);
		ui::add_button("reset changes button", nav_container, "Reset changes", fonts::dejavu, [] {
			ui::reset_tied_sliders();
			settings = current_global_settings;
			app_settings = current_app_settings;
			preset_settings = current_preset_settings;
			on_load();
		});
	}

	if (config_not_default) {
		ui::set_next_same_line(nav_container);
		ui::add_button("restore defaults button", nav_container, "Restore defaults", fonts::dejavu, [] {
			ui::reset_tied_sliders();
			settings = config_blur::DEFAULT_CONFIG;
			app_settings = config_app::DEFAULT_CONFIG;
			preset_settings = config_presets::DEFAULT_CONFIG;
			parse_interp();
		});
	}

	auto on_tab_select = [&config_container] {
		config_container.scroll_to_top = true;
	};

	auto* config_tabs =
		ui::add_tabs("config tabs", config_container, CONFIG_TABS, selected_config_tab, fonts::dejavu, on_tab_select);

	ui::center_element(config_container, config_tabs);

	ui::add_spacing(config_container, 3);

	if (selected_config_tab == "blur") {
		options(config_container);
		preview(preview_header_container, preview_content_container);
	}
	else if (selected_config_tab == "app") {
		app_options(config_container);
		about(preview_content_container);
	}
	else {
		preset_options(config_container);
	}

	option_information(option_information_container);
}
