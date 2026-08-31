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
}

void configs::config_management(ui::Container& container) {
	if (edited_configs.empty())
		return;

	auto names = taken_names();

	if (!u::contains(names, selected_config_name))
		select_config(names.front());

	bool is_default = app_settings.default_config == selected_config_name;

	// the dropdown holds onto a pointer to this, so it has to outlive the frame
	static std::string selected;
	selected = selected_config_name;

	ui::add_dropdown(
		"blur config dropdown", container, "config", names, selected, fonts::dejavu, [](std::string* new_value) {
			select_config(*new_value);
		}
	);

	// collected first and laid out below: five buttons don't fit on one line at this panel width, and
	// center_elements only shifts a row rather than wrapping it, so they'd hang off both edges
	struct Action {
		std::string id;
		std::string label;
		std::function<void()> on_press;
	};

	std::vector<Action> actions;

	auto add_action = [&](const std::string& id, const std::string& label, std::function<void()> on_press) {
		actions.emplace_back(Action{ .id = id, .label = label, .on_press = std::move(on_press) });
	};

	add_action("new config button", "New", [] {
		open_name_dialog("New config", "Create", unique_name("new config"), "new config", {}, [](const auto& name) {
			// built-in defaults rather than a copy of what's selected - that's what Duplicate is for
			add_config(name, config_blur::DEFAULT_CONFIG);
		});
	});

	add_action("duplicate config button", "Duplicate", [] {
		flush_selected_config();

		open_name_dialog(
			"Duplicate config",
			"Duplicate",
			unique_name(selected_config_name),
			"duplicate config",
			{},
			[source = settings](const auto& name) {
				add_config(name, source);
			}
		);
	});

	add_action("rename config button", "Rename", [] {
		open_name_dialog(
			"Rename config",
			"Rename",
			selected_config_name,
			"rename config",
			selected_config_name,
			[from = selected_config_name](const auto& to) {
				if (from == to)
					return;

				flush_selected_config();

				auto node = edited_configs.extract(from);
				if (node.empty())
					return;

				node.key() = to;
				edited_configs.insert(std::move(node));

				// the default is stored by name, so it has to follow the rename. the file on disk is
			    // renamed at save time, along with the app config this writes into
				if (app_settings.default_config == from)
					app_settings.default_config = to;

				selected_config_name = to;
			}
		);
	});

	// there's always got to be something to select, and something for videos to render with
	if (edited_configs.size() > 1) {
		add_action("delete config button", "Delete", [] {
			ui::dialog::confirm_destructive(
				"Remove config?",
				std::format("'{}' will be removed.", selected_config_name),
				"Remove",
				[name = selected_config_name] {
					edited_configs.erase(name);

					if (edited_configs.empty())
						return;

					// if the default went with it, something has to take over, or videos would queue
				    // against a config that isn't there
					if (app_settings.default_config == name)
						app_settings.default_config = edited_configs.begin()->first;

					selected_config_name.clear(); // so select_config doesn't take it for a no-op
					select_config(edited_configs.begin()->first);
				}
			);
		});
	}

	if (!is_default) {
		add_action("set default config button", "Set default", [] {
			app_settings.default_config = selected_config_name;
		});
	}

	// greedily pack into as many centred lines as it takes
	const int usable_width = container.get_usable_rect().w;

	std::vector<ui::AnimatedElement*> row;
	int row_width = 0;

	auto flush_row = [&] {
		if (row.empty())
			return;

		ui::center_elements(container, row);
		row.clear();
		row_width = 0;
	};

	for (const auto& action : actions) {
		int width = ui::button_width(action.label, fonts::dejavu);
		int width_on_this_row = row.empty() ? width : row_width + container.element_gap + width;

		if (!row.empty() && width_on_this_row > usable_width)
			flush_row();

		if (!row.empty())
			ui::set_next_same_line(container);

		row.push_back(ui::add_button(action.id, container, action.label, fonts::dejavu, action.on_press));
		row_width = row.size() == 1 ? width : row_width + container.element_gap + width;
	}

	flush_row();

	if (is_default) {
		ui::add_text(
			"default config notice",
			container,
			"videos start on this config",
			gfx::Color::white(renderer::MUTED_SHADE),
			fonts::dejavu,
			FONT_CENTERED_X
		);
	}
}
