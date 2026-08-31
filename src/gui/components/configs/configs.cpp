#include "configs.h"
#include "../../renderer.h"

#include "../../ui/ui.h"
#include "../../render/render.h"
#include "../notifications.h"

namespace configs = gui::components::configs;

namespace {
	const gfx::Size ACTION_ICON_SIZE(20, 20);
	const gfx::Color ACTION_ICON_COLOR = gfx::Color::white(120);
	const gfx::Color ACTION_ICON_HOVER_COLOR = gfx::Color::white();

	void copy_to_clipboard(const std::string& text, const std::string& label) {
		SDL_SetClipboardText(text.c_str());

		gui::components::notifications::add(
			std::format("Exported {} to clipboard", label),
			ui::NotificationType::INFO,
			{},
			std::chrono::duration<float>(2.f)
		);
	}

	std::optional<std::string> get_clipboard_text() {
		size_t len = 0;
		void* clipboard_data = SDL_GetClipboardData("text/plain", &len);

		if (!clipboard_data || len == 0) {
			gui::components::notifications::add(
				"Clipboard is empty or unreadable",
				ui::NotificationType::NOTIF_ERROR,
				{},
				std::chrono::duration<float>(2.f)
			);

			return {};
		}

		std::string text(static_cast<char*>(clipboard_data), len);
		SDL_free(clipboard_data);

		return text;
	}
}

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

void configs::config_actions(ui::Container& container) {
	const std::string& tab = selected_config_tab;

	std::string label;
	std::function<std::string()> do_export;
	std::function<void(const std::string&)> do_import;
	std::optional<std::function<void()>> do_restore_defaults;

	if (tab == "blur") {
		label = "blur config";

		do_export = [] {
			return config_blur::export_concise(settings);
		};

		do_import = [](const std::string& text) {
			auto imported = config_blur::parse(text);

			ui::reset_tied_sliders();
			settings = imported;
			parse_interp();
		};

		if (settings != config_blur::DEFAULT_CONFIG) {
			do_restore_defaults = [] {
				ui::reset_tied_sliders();
				settings = config_blur::DEFAULT_CONFIG;
				parse_interp();
			};
		}
	}
	else if (tab == "app") {
		label = "app config";

		do_export = [] {
			return config_app::export_shareable(app_settings);
		};

		do_import = [](const std::string& text) {
			auto imported = config_app::parse(text);
			config_app::copy_machine_settings(imported, app_settings);
			app_settings = imported;
		};

		auto modified_defaults = config_app::DEFAULT_CONFIG;
		config_app::copy_machine_settings(modified_defaults, app_settings);

		if (app_settings != modified_defaults) {
			do_restore_defaults = [modified_defaults] {
				app_settings = modified_defaults;
			};
		}
	}
	else {
		label = "presets";

		do_export = [] {
			return config_presets::generate_config_string(preset_settings);
		};

		do_import = [](const std::string& text) {
			preset_settings = config_presets::parse(text);
		};

		if (preset_settings != config_presets::DEFAULT_CONFIG) {
			do_restore_defaults = [] {
				preset_settings = config_presets::DEFAULT_CONFIG;
			};
		}
	}

	std::vector<ui::AnimatedElement*> buttons;

	auto add_action_button = [&](const std::string& id,
	                             const std::string& icon,
	                             const std::string& tooltip,
	                             std::function<void()> on_press) {
		if (!buttons.empty())
			ui::set_next_same_line(container);

		buttons.push_back(
			ui::add_icon_button(
				std::format("{} {}", tab, id),
				container,
				icon,
				fonts::icons,
				ACTION_ICON_SIZE,
				ACTION_ICON_COLOR,
				ACTION_ICON_HOVER_COLOR,
				std::move(on_press),
				tooltip
			)
		);
	};

	add_action_button(
		"export config button", EXPORT_ICON, std::format("Export {} to clipboard", label), [do_export, label] {
			copy_to_clipboard(do_export(), label);
		}
	);

	add_action_button(
		"import config button", IMPORT_ICON, std::format("Import {} from clipboard", label), [do_import, label] {
			auto text = get_clipboard_text();
			if (!text)
				return;

			try {
				do_import(*text);

				gui::components::notifications::add(
					std::format("Imported {} from clipboard", label),
					ui::NotificationType::INFO,
					{},
					std::chrono::duration<float>(2.f)
				);
			}
			catch (const std::exception& e) {
				gui::components::notifications::add(
					std::format("Failed to load {}: {}", label, e.what()),
					ui::NotificationType::NOTIF_ERROR,
					{},
					std::chrono::duration<float>(3.f)
				);
			}
		}
	);

	if (do_restore_defaults) {
		add_action_button(
			"restore defaults button",
			RESTORE_DEFAULTS_ICON,
			std::format("Restore default {}", label),
			[do_restore_defaults] {
				if (do_restore_defaults)
					(*do_restore_defaults)();
			}
		);
	}

	ui::center_elements(container, buttons);
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

	bool config_changed = settings != current_global_settings || app_settings != current_app_settings ||
	                      preset_settings != current_preset_settings;

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

	auto on_tab_select = [&config_container] {
		config_container.scroll_to_top = true;
	};

	config_container.push_element_gap(2);
	{
		auto* config_tabs = ui::add_tabs(
			"config tabs", config_container, CONFIG_TABS, selected_config_tab, fonts::dejavu, on_tab_select
		);

		ui::center_element(config_container, config_tabs);

		config_actions(config_container);
	}
	config_container.pop_element_gap();

	if (selected_config_tab == "blur") {
		options(config_container);
		preview(preview_header_container, preview_content_container);
	}
	else if (selected_config_tab == "app") {
		app_options(config_container);

		if (theme_preview_open(config_container))
			theme_preview(preview_content_container);
		else
			about(preview_content_container);
	}
	else {
		preset_options(config_container);
	}

	option_information(option_information_container);
}
