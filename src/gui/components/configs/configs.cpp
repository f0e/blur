#include "configs.h"

#include "../../ui/ui.h"
#include "../../render/render.h"
#include "../notifications.h"

namespace configs = gui::components::configs;

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

	bool config_changed = settings != current_global_settings || app_settings != current_app_settings;
	bool config_not_default = settings != config_blur::DEFAULT_CONFIG || app_settings != modified_default_app;

	if (config_changed) {
		ui::set_next_same_line(nav_container);
		ui::add_button("save button", nav_container, "Save", fonts::dejavu, [&] {
			save_config();
		});

		ui::set_next_same_line(nav_container);
		ui::add_button("reset changes button", nav_container, "Reset changes", fonts::dejavu, [&] {
			ui::reset_tied_sliders();
			settings = current_global_settings;
			app_settings = current_app_settings;
			on_load();
		});
	}

	if (config_not_default) {
		ui::set_next_same_line(nav_container);
		ui::add_button("restore defaults button", nav_container, "Restore defaults", fonts::dejavu, [] {
			ui::reset_tied_sliders();
			settings = config_blur::DEFAULT_CONFIG;
			app_settings = config_app::DEFAULT_CONFIG;
			parse_interp();
		});
	}

	options(config_container);
	preview(preview_header_container, preview_content_container);
	option_information(option_information_container);
}
