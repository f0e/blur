#include "configs.h"

#include "../../ui/ui.h"
#include "../../renderer.h"

namespace configs = gui::components::configs;

namespace {
	const gfx::Color ERROR_TEXT_COLOR(255, 80, 80, 255);

	// a config the user is naming, either a new one or one being renamed. lives past the frame that opened
	// the dialog, since the dialog's content runs every frame and the text input edits this in place
	struct NameDialogState {
		std::string name;
		std::string error;
	};

	// the names taken right now: what's on disk plus anything added this session, minus anything removed.
	// config_blur::list() alone would miss both, and a name clash between two unsaved configs would only
	// come out at save time as one silently overwriting the other
	std::vector<std::string> taken_names() {
		std::vector<std::string> names;
		names.reserve(configs::edited_configs.size());

		for (const auto& [name, config] : configs::edited_configs) {
			names.push_back(name);
		}

		return names;
	}

	bool name_taken(const std::string& name) {
		return std::ranges::any_of(taken_names(), [&](const std::string& existing) {
			return u::to_lower(existing) == u::to_lower(name);
		});
	}

	std::string unique_name(const std::string& name) {
		if (!name_taken(name))
			return name;

		for (int i = 2;; i++) {
			std::string candidate = std::format("{} {}", name, i);
			if (!name_taken(candidate))
				return candidate;
		}
	}

	// shared by the new/duplicate/rename dialogs - they only differ in what they do with the name
	void open_name_dialog(
		const std::string& title,
		const std::string& confirm_text,
		const std::string& initial_name,
		const std::string& id_prefix,
		// the name being edited, if this is a rename - it's allowed to keep its own name
		const std::optional<std::string>& existing_name,
		const std::function<void(const std::string& name)>& on_accept
	) {
		auto state = std::make_shared<NameDialogState>(NameDialogState{ .name = initial_name });

		ui::dialog::open(
			{
				.title = title,
				.content =
					[state, id_prefix](ui::Container& container) {
						ui::add_text_input(
							std::format("{} name input", id_prefix),
							container,
							state->name,
							"name",
							fonts::dejavu,
							"",
							[state](const std::string&) {
								state->error.clear();
							}
						);

						if (!state->error.empty()) {
							ui::add_text(
								std::format("{} name error", id_prefix),
								container,
								state->error,
								ERROR_TEXT_COLOR,
								fonts::dejavu(fonts::size::SMALL)
							);
						}
					},
				.action_required = true,
				.close_on_confirm = false,
				.confirm_text = confirm_text,
				.on_confirm =
					[state, existing_name, on_accept] {
						// a config is one file per name, so whatever's typed has to work as a filename
						auto valid = u::validate_filename(state->name);
						if (!valid) {
							state->error = valid.error();
							return;
						}

						bool keeping_own_name = existing_name && u::to_lower(*existing_name) == u::to_lower(*valid);

						if (!keeping_own_name && name_taken(*valid)) {
							state->error = "A config with that name already exists.";
							return;
						}

						ui::dialog::close();
						on_accept(*valid);
					},
			}
		);
	}

	void add_config(const std::string& name, const BlurSettings& settings) {
		configs::edited_configs[name] = settings;
		configs::select_config(name);
	}

	void rename_config(const std::string& name) {
		open_name_dialog("Rename config", "Rename", name, "rename config", name, [from = name](const auto& to) {
			if (from == to)
				return;

			configs::flush_selected_config();

			auto node = configs::edited_configs.extract(from);
			if (node.empty())
				return;

			node.key() = to;
			configs::edited_configs.insert(std::move(node));

			// config files are renamed on save
			if (configs::app_settings.default_config == from)
				configs::app_settings.default_config = to;

			if (configs::selected_config_name == from)
				configs::selected_config_name = to;

			config_rules::rename_config(configs::rule_settings, from, to);
		});
	}

	void duplicate_config(const std::string& name) {
		// duplicate unsaved edits rather than the last saved version
		configs::flush_selected_config();

		auto it = configs::edited_configs.find(name);
		if (it == configs::edited_configs.end())
			return;

		open_name_dialog(
			"Duplicate config",
			"Duplicate",
			unique_name(name),
			"duplicate config",
			{},
			[source = it->second](const auto& new_name) {
				add_config(new_name, source);
			}
		);
	}

	void delete_config(const std::string& name) {
		ui::dialog::confirm_destructive("Remove config?", std::format("'{}' will be removed.", name), "Remove", [name] {
			configs::edited_configs.erase(name);

			// deleting the default intentionally leaves no default
			if (configs::app_settings.default_config == name)
				configs::app_settings.default_config.clear();

			if (configs::edited_configs.empty())
				return;

			if (configs::selected_config_name == name) {
				// force select_config() to load the replacement
				configs::selected_config_name.clear();
				configs::select_config(configs::edited_configs.begin()->first);
			}
		});
	}

	void new_config() {
		open_name_dialog("New config", "Create", unique_name("new config"), "new config", {}, [](const auto& name) {
			add_config(name, config_blur::DEFAULT_CONFIG);
		});
	}
}

void configs::config_management(ui::Container& container) {
	if (edited_configs.empty())
		return;

	auto names = taken_names();

	if (!u::contains(names, selected_config_name))
		select_config(names.front());

	// the dropdown holds onto a pointer to this, so it has to outlive the frame
	static std::string selected;
	selected = selected_config_name;

	auto is_default = [](const std::string& name) {
		return app_settings.default_config == name;
	};

	std::vector<ui::DropdownOptionAction> row_actions = {
		{
			.icon = icons::STAR,
			.tooltip = "Default config (click to unset)",
			.color = DEFAULT_CONFIG_ICON_COLOR,
			.hover_color = DEFAULT_CONFIG_ICON_COLOR,
			.on_press =
				[](const std::string&) {
					app_settings.default_config.clear();
				},
			.applies_to = is_default,
		},
		{
			.icon = icons::STAR,
			.tooltip = "Set as default config",
			.hover_color = DEFAULT_CONFIG_ICON_COLOR,
			.on_press =
				[](const std::string& name) {
					app_settings.default_config = name;
				},
			.applies_to =
				[is_default](const std::string& name) {
					return !is_default(name);
				},
		},
		{
			.icon = icons::RENAME,
			.tooltip = "Rename config",
			.on_press = rename_config,
		},
		{
			.icon = icons::COPY,
			.tooltip = "Duplicate config",
			.on_press = duplicate_config,
		},
		{
			.icon = icons::TRASH,
			.tooltip = "Delete config",
			.hover_color = DELETE_ICON_HOVER_COLOR,
			.on_press = delete_config,
			.applies_to =
				[](const std::string&) {
					return edited_configs.size() > 1;
				},
		},
	};

	auto* dropdown = ui::add_dropdown(
		"blur config dropdown",
		container,
		"",
		names,
		selected,
		fonts::dejavu,
		[](std::string* new_value) {
			select_config(*new_value);
		},
		{},
		row_actions,
		ui::DropdownAddAction{
			.tooltip = "New config",
			.on_press = new_config,
		}
	);

	const auto& dropdown_data = std::get<ui::DropdownElementData>(dropdown->element->data);
	hovered_config = dropdown_data.hovered_option;

	set_temporary_tab("blur config dropdown", dropdown->animations.at(ui::hasher("expand")).goal > 0, TABS[2]);
}
