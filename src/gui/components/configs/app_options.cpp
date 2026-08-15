#include "configs.h"

#include "../../ui/ui.h"
#include "../../render/render.h"

#include "common/config_app.h"

namespace configs = gui::components::configs;

void configs::app_options(ui::Container& container) {
	bool first_section = true;
	auto section_component = [&](const std::string& label, bool* setting = nullptr, bool forced_on = false) {
		section(container, first_section, label, setting, forced_on);
	};

	/*
	    Interface
	*/
	section_component("interface");

	ui::add_slider(
		"dpi scale slider",
		container,
		0.f,
		3.f,
		&app_settings.dpi_scale_override,
		"dpi scale: {:.2f}",
		fonts::dejavu,
		{},
		0.05f,
		"0 = automatic"
	);

	/*
	    Preview
	*/
	section_component("preview");

	ui::add_slider(
		"queue preview volume slider",
		container,
		0,
		100,
		&app_settings.preview_volume,
		"queue preview volume: {}%",
		fonts::dejavu
	);

	/*
	    Rendering
	*/
	section_component("rendering");

	ui::add_checkbox("skip queue checkbox", container, "skip queue", app_settings.skip_queue, fonts::dejavu);

	/*
	    Notifications
	*/
	section_component("notifications");

	ui::add_checkbox(
		"render success notifications checkbox",
		container,
		"render success notifications",
		app_settings.render_success_notifications,
		fonts::dejavu
	);

	ui::add_checkbox(
		"render failure notifications checkbox",
		container,
		"render failure notifications",
		app_settings.render_failure_notifications,
		fonts::dejavu
	);

	ui::add_checkbox(
		"config override notification checkbox",
		container,
		"notify about config overrides",
		app_settings.notify_about_config_override,
		fonts::dejavu
	);

	/*
	    Updates
	*/
	section_component("updates");

	ui::add_checkbox(
		"check for updates checkbox",
		container,
		"automatically check for updates",
		app_settings.check_updates,
		fonts::dejavu
	);

	if (app_settings.check_updates) {
		ui::add_checkbox(
			"include beta updates checkbox", container, "check for beta updates", app_settings.check_beta, fonts::dejavu
		);

		if (!app_settings.dismissed_update_version.empty()) {
			ui::add_button(
				"clear dismissed update button",
				container,
				std::format("Un-dismiss update {}", app_settings.dismissed_update_version),
				fonts::dejavu,
				[] {
					app_settings.dismissed_update_version.clear();
				}
			);
		}
	}

#ifdef __linux__
	/*
	    Linux
	*/
	section_component("linux");

	ui::add_text_input(
		"vapoursynth lib path input",
		container,
		app_settings.vapoursynth_lib_path,
		"vapoursynth lib path",
		fonts::dejavu
	);
#endif
}
