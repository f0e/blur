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

namespace {
	// text inputs and dropdowns keep a pointer to the string they're showing, and elements stick around for a
	// bit after they stop being added (they fade out), so the strings can't live in the vector they came from -
	// removing an entry would leave the fading out elements pointing at freed memory. they live here instead,
	// keyed by element id, and never get erased
	struct InputBuffer {
		std::string text;   // what the element edits
		std::string synced; // the value both sides last agreed on, used to work out which way to sync
	};

	std::map<std::string, InputBuffer> input_buffers;

	// same idea for checkboxes, which keep a bool* and would dangle when the vector behind them grows
	struct CheckboxBuffer {
		bool value = false;
		bool synced = false;
	};

	std::map<std::string, CheckboxBuffer> checkbox_buffers;
}

std::string& configs::bind_input(const std::string& id, std::string& value) {
	auto& buffer = input_buffers[id];

	if (buffer.text != buffer.synced) {
		// edited in the ui
		value = buffer.text;
	}
	else if (value != buffer.synced) {
		// changed elsewhere (config loaded, changes reset, an entry above this one was removed, ...)
		buffer.text = value;
	}

	buffer.synced = value;

	return buffer.text;
}

std::string& configs::bind_read_only_input(const std::string& id, const std::string& value) {
	auto& buffer = input_buffers[id];

	buffer.text = value;
	buffer.synced = value;

	return buffer.text;
}

bool& configs::bind_checkbox(const std::string& id, bool& value) {
	auto& buffer = checkbox_buffers[id];

	if (buffer.value != buffer.synced)
		value = buffer.value;
	else if (value != buffer.synced)
		buffer.value = value;

	buffer.synced = value;

	return buffer.value;
}

namespace {
	// whether the owner asked for the switch this frame. an owner that stops being drawn while it
	// holds the switch never gets to close it, which would leave the panel stuck on its tab
	bool temp_tab_owner_drawn = false;
}

void configs::set_temporary_tab(const std::string& owner, bool open, const std::string& tab) {
	if (temp_tab_owner == owner)
		temp_tab_owner_drawn = true;

	if (open) {
		if (temp_tab_owner.empty()) {
			temp_tab_owner = owner;
			temp_tab_owner_drawn = true;
			old_tab = selected_tab;
		}

		if (temp_tab_owner == owner)
			selected_tab = tab;
	}
	else if (temp_tab_owner == owner) {
		selected_tab = old_tab;
		old_tab.clear();
		temp_tab_owner.clear();
	}
}

void configs::release_stale_temporary_tab() {
	if (!temp_tab_owner.empty() && !temp_tab_owner_drawn) {
		selected_tab = old_tab;
		old_tab.clear();
		temp_tab_owner.clear();
	}

	temp_tab_owner_drawn = false;
}

void configs::flush_selected_config() {
	if (selected_config_name.empty())
		return;

	edited_configs[selected_config_name] = settings;
}

void configs::select_config(const std::string& name) {
	if (name == selected_config_name)
		return;

	flush_selected_config(); // keep the edits on the config being left

	selected_config_name = name;

	auto it = edited_configs.find(name);
	settings = it != edited_configs.end() ? it->second : config_blur::DEFAULT_CONFIG;

	// the sliders cache the values they were bound to, so they'd keep showing the old config's numbers
	ui::reset_tied_sliders();
	parse_interp();
}

bool configs::has_unsaved_changes() {
	flush_selected_config();

	return edited_configs != saved_configs || app_settings != current_app_settings ||
	       rule_settings != current_rule_settings || encoding_preset_settings != current_encoding_preset_settings;
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
		label = "encoding presets";

		do_export = [] {
			return config_encoding_presets::generate_config_string(encoding_preset_settings);
		};

		do_import = [](const std::string& text) {
			encoding_preset_settings = config_encoding_presets::parse(text);
		};

		if (encoding_preset_settings != config_encoding_presets::DEFAULT_CONFIG) {
			do_restore_defaults = [] {
				encoding_preset_settings = config_encoding_presets::DEFAULT_CONFIG;
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
		"export config button", icons::EXPORT, std::format("Export {} to clipboard", label), [do_export, label] {
			copy_to_clipboard(do_export(), label);
		}
	);

	add_action_button(
		"import config button", icons::IMPORT, std::format("Import {} from clipboard", label), [do_import, label] {
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
			icons::RESTORE_DEFAULTS,
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
	ui::Container& preview_content_container,
	ui::Container& option_information_container,
	float delta_time
) {
	auto on_tab_select = [&config_container] {
		config_container.scroll_to_top = true;
	};

	config_container.push_element_gap(2);
	{
		auto* config_tabs = ui::add_tabs(
			"config tabs", config_container, CONFIG_TABS, selected_config_tab, fonts::dejavu, on_tab_select
		);

		if (selected_config_tab == "encoding presets")
			ui::center_element(config_container, config_tabs);
		else {
			const auto usable_rect = config_container.get_usable_rect();
			const int tabs_region_x = usable_rect.x + ui::tabs_height(fonts::dejavu) + CONFIG_HEADER_NAV_GAP;
			const int tabs_region_width = usable_rect.x2() - tabs_region_x;

			config_tabs->element->rect.x = tabs_region_x + (tabs_region_width - config_tabs->element->rect.w) / 2;
			config_tabs->element->orig_rect.x = config_tabs->element->rect.x;
		}
	}
	config_container.pop_element_gap();

	static bool loading_config = false;
	if (!loaded_config) {
		if (!loading_config) {
			loading_config = true;

			std::thread([] {
				ui::reset_tied_sliders();

				edited_configs.clear();
				for (const auto& name : config_blur::list()) {
					edited_configs[name] = config_blur::get_config(name);
				}

				selected_config_name = config_blur::get_default_name();
				if (!edited_configs.contains(selected_config_name) && !edited_configs.empty())
					selected_config_name = edited_configs.begin()->first;

				settings = edited_configs.contains(selected_config_name) ? edited_configs[selected_config_name]
				                                                         : config_blur::DEFAULT_CONFIG;

				app_settings = config_app::get_app_config();
				encoding_preset_settings = config_encoding_presets::get_config();
				rule_settings = config_rules::get_config();
				on_load();
				loading_config = false;
				loaded_config = true;
			}).detach();
		}

		const int content_y = config_container.current_position.y;
		auto* loading_text = ui::add_text(
			"config loading text",
			config_container,
			"Loading config...",
			gfx::Color::white(100),
			fonts::dejavu,
			FONT_CENTERED_X
		);

		const auto usable_rect = config_container.get_usable_rect();
		loading_text->element->rect.y = content_y + (usable_rect.y2() - content_y - loading_text->element->rect.h) / 2;
		loading_text->element->orig_rect.y = loading_text->element->rect.y;

		// stuff that doesnt need configs to be loaded
		if (selected_config_tab == "blur") {
			preview_tabs(preview_content_container);
		}
		else if (selected_config_tab == "app") {
			about(preview_content_container);
		}

		return;
	}

	if (has_unsaved_changes()) {
		ui::set_next_same_line(nav_container);
		ui::add_button("save button", nav_container, "Save", fonts::dejavu, [] {
			// saving would silently drop the broken presets, send the user to fix them instead
			if (auto preset_error = config_encoding_presets::validate(encoding_preset_settings)) {
				selected_config_tab = "encoding presets";
				selected_encoding_preset_gpu_type = preset_error->gpu_type;

				gui::components::notifications::add(
					"preset errors", "Fix the errors in your presets before saving", ui::NotificationType::NOTIF_ERROR
				);

				return;
			}

			// every config gets validated, not just the one on screen - saving writes them all, so a
			// broken one that isn't selected would otherwise be written or dropped without a word
			flush_selected_config();

			for (auto& [name, config] : edited_configs) {
				auto validation = config_blur::validate(config, app_settings, encoding_preset_settings, false);
				if (validation.ok())
					continue;

				selected_config_tab = "blur";
				selected_tab = TABS[0];
				select_config(name); // show the config the error is actually in

				gui::components::notifications::add(
					"settings error",
					edited_configs.size() > 1 ? std::format("{}: {}", name, validation.message())
											  : validation.message(),
					ui::NotificationType::NOTIF_ERROR
				);

				return;
			}

			save_config();
		});

		ui::set_next_same_line(nav_container);
		ui::add_button("reset changes button", nav_container, "Reset changes", fonts::dejavu, [] {
			ui::reset_tied_sliders();

			edited_configs = saved_configs;

			// the selected config may have been one added this session, which reverting just removed
			if (!edited_configs.contains(selected_config_name))
				selected_config_name = edited_configs.empty() ? "" : edited_configs.begin()->first;

			settings = edited_configs.contains(selected_config_name) ? edited_configs[selected_config_name]
			                                                         : config_blur::DEFAULT_CONFIG;

			app_settings = current_app_settings;
			encoding_preset_settings = current_encoding_preset_settings;
			rule_settings = current_rule_settings;
			on_load();
		});
	}

	config_container.push_element_gap(2);
	if (selected_config_tab == "blur")
		config_management(config_container);

	config_actions(config_container);
	config_container.pop_element_gap();

	if (selected_config_tab == "blur") {
		ui::add_separator("config management separator", config_container, ui::SeparatorStyle::FADE_RIGHT);

		options(config_container);
		preview(preview_content_container);
	}
	else if (selected_config_tab == "app") {
		app_options(config_container);

		if (theme_preview_open(config_container))
			theme_preview(preview_content_container);
		else
			about(preview_content_container);
	}
	else {
		encoding_preset_options(config_container);
	}

	option_information(option_information_container);
}
