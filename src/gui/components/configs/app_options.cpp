#include "configs.h"

#include "../../ui/ui.h"
#include "../../render/render.h"
#include "../../renderer.h"

#include "common/config_app.h"
#include "gui/sdl.h"

namespace configs = gui::components::configs;

#ifdef BLUR_COLOR_THEMES
namespace {
	const std::string THEME_PICKER_ID = "color theme picker";

	// the default color plus the rest of the wheel in the same tone
	const std::vector<gfx::Color> THEME_PRESETS = [] {
		std::vector<gfx::Color> presets = { ui::DEFAULT_HIGHLIGHT_COLOR };

		float saturation = ui::DEFAULT_HIGHLIGHT_COLOR.saturation();
		float brightness = ui::DEFAULT_HIGHLIGHT_COLOR.brightness();

		const int hues = 10;
		for (int i = 1; i < hues; i++) { // the default takes hue 0
			presets.push_back(gfx::Color::from_hsb(float(i) / hues, saturation, brightness));
		}

		return presets;
	}();
}
#endif

void configs::app_options(ui::Container& container) {
#ifdef BLUR_COLOR_THEMES
	// preview the picked theme straight away rather than waiting for the config to be saved
	ui::highlight_color =
		gfx::Color::from_hex_string(app_settings.gui_color_hex, false).value_or(ui::DEFAULT_HIGHLIGHT_COLOR);
#endif

	bool first_section = true;
	auto section_component = [&](const std::string& label, bool* setting = nullptr, bool forced_on = false) {
		section(container, first_section, label, setting, forced_on);
	};

	/*
	    Interface
	*/
	section_component("interface");

#ifdef BLUR_COLOR_THEMES
	ui::add_color_picker(
		THEME_PICKER_ID,
		container,
		"color theme",
		app_settings.gui_color_hex,
		fonts::dejavu,
		ui::DEFAULT_HIGHLIGHT_COLOR,
		THEME_PRESETS
	);
#endif

	ui::add_slider(
		"window width slider",
		container,
		sdl::MINIMUM_WINDOW_SIZE.w,
		sdl::MINIMUM_WINDOW_SIZE.w * 3,
		&app_settings.gui_width,
		"default window width: {}",
		fonts::dejavu
	);

	ui::add_slider(
		"window height slider",
		container,
		sdl::MINIMUM_WINDOW_SIZE.h,
		sdl::MINIMUM_WINDOW_SIZE.h * 3,
		&app_settings.gui_height,
		"default window height: {}",
		fonts::dejavu
	);

	ui::add_button("use current window size button", container, "use current window size", fonts::dejavu, [] {
		int width = 0;
		int height = 0;
		SDL_GetWindowSize(sdl::window, &width, &height);

		const float content_scale = render::get_content_scale(sdl::window);
		app_settings.gui_width = std::lround(width / content_scale);
		app_settings.gui_height = std::lround(height / content_scale);
	});

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

	ui::add_checkbox(
		"preview hardware decoding checkbox",
		container,
		"video preview hardware accel",
		app_settings.preview_hardware_decoding,
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
		"taskbar progress checkbox",
		container,
		"taskbar icon render progress",
		app_settings.taskbar_progress,
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

bool configs::theme_preview_open(const ui::Container& config_container) {
#ifdef BLUR_COLOR_THEMES
	return ui::is_color_picker_open(config_container, THEME_PICKER_ID);
#else
	return false;
#endif
}

void configs::theme_preview([[maybe_unused]] ui::Container& container) {
#ifdef BLUR_COLOR_THEMES
	static float example_value = 0.6f;
	static bool example_toggle = true;

	ui::add_text(
		"theme preview title",
		container,
		"theme preview",
		gfx::Color::white(renderer::MUTED_SHADE),
		fonts::dejavu,
		FONT_CENTERED_X
	);

	ui::add_slider(
		"theme preview slider", container, 0.f, 1.f, &example_value, "example slider: {:.2f}", fonts::dejavu, {}, 0.01f
	);

	ui::add_checkbox("theme preview checkbox", container, "example checkbox", example_toggle, fonts::dejavu);
#endif
}
