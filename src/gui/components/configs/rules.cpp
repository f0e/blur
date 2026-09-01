#include "configs.h"

#include "../../ui/ui.h"
#include "../../renderer.h"

namespace configs = gui::components::configs;

namespace {
	const std::string CONFIG_DROPDOWN_OWNER = "blur config dropdown";

	std::vector<std::string> config_options(const std::string& current) {
		std::vector<std::string> names;
		names.reserve(configs::edited_configs.size() + 1);

		for (const auto& [name, config] : configs::edited_configs) {
			names.push_back(name);
		}

		// keep a rule's config in its own dropdown even once it's gone, so the rule shows what it's
		// pointing at rather than snapping to whichever config happens to sort first
		if (!current.empty() && !u::contains(names, current))
			names.push_back(current);

		return names;
	}

	bool config_missing(const std::string& name) {
		return !name.empty() && !configs::edited_configs.contains(name);
	}

	void rule_row(ui::Container& container, size_t index) {
		auto& rule = configs::rule_settings.rules[index];

		std::string id = std::format("rule {}", index);

		int icon_size = ui::text_input_height(fonts::dejavu);
		auto icon_dimensions = gfx::Size(icon_size, icon_size);

		container.push_element_gap(4);

		ui::add_text_input(
			std::format("{} pattern input", id),
			container,
			configs::bind_input(std::format("{} pattern", id), rule.pattern),
			"",
			fonts::dejavu,
			"pattern",
			{},
			false,
			container.get_usable_rect().w - icon_size - configs::DELETE_ICON_GAP
		);

		ui::set_next_same_line(container);

		auto* delete_button = ui::add_icon_button(
			std::format("{} delete button", id),
			container,
			icons::CLOSE,
			fonts::icons,
			icon_dimensions,
			configs::DELETE_ICON_COLOR,
			configs::DELETE_ICON_HOVER_COLOR,
			[index] {
				if (index < configs::rule_settings.rules.size())
					configs::rule_settings.rules.erase(configs::rule_settings.rules.begin() + index);
			},
			"Remove rule"
		);

		ui::right_align_element(container, delete_button);

		container.pop_element_gap();

		std::optional<std::string> message;
		gfx::Color message_color = configs::WARNING_COLOR;

		if (config_missing(rule.config_name)) {
			message = std::format("rule disabled - '{}' no longer exists", rule.config_name);
			message_color = configs::ERROR_COLOR;
		}
		else if (u::trim(rule.pattern).empty()) {
			message = "enter a pattern for this rule to match";
		}

		configs::add_with_message(container, std::format("{} message", id), message, message_color, [&] {
			container.push_element_gap(4);
			container.push_usable_width(0.55f);

			ui::add_dropdown(
				std::format("{} config dropdown", id),
				container,
				"",
				config_options(rule.config_name),
				configs::bind_read_only_input(std::format("{} config", id), rule.config_name),
				fonts::dejavu,
				[index](std::string* new_value) {
					if (index < configs::rule_settings.rules.size())
						configs::rule_settings.rules[index].config_name = *new_value;
				},
				config_missing(rule.config_name) ? std::vector{ rule.config_name } : std::vector<std::string>{}
			);

			container.pop_usable_width();

			auto reorder_button =
				[&](const std::string& button_id, const std::string& tooltip, float rotation, size_t swap_with) {
					ui::set_next_same_line(container);

					return ui::add_icon_button(
						std::format("{} {}", id, button_id),
						container,
						icons::DROPDOWN_ARROW,
						fonts::icons,
						icon_dimensions,
						configs::DELETE_ICON_COLOR,
						gfx::Color::white(),
						[index, swap_with] {
							auto& rules = configs::rule_settings.rules;
							if (index < rules.size() && swap_with < rules.size())
								std::swap(rules[index], rules[swap_with]);
						},
						tooltip,
						rotation
					);
				};

			std::vector<ui::AnimatedElement*> reorder_buttons;

			if (index > 0)
				reorder_buttons.push_back(reorder_button("move up button", "Move up", 180.f, index - 1));

			if (index + 1 < configs::rule_settings.rules.size())
				reorder_buttons.push_back(reorder_button("move down button", "Move down", 0.f, index + 1));

			if (!reorder_buttons.empty())
				ui::right_align_element(container, reorder_buttons.back());

			container.pop_element_gap();
		});

		ui::add_checkbox(
			std::format("{} enabled checkbox", id),
			container,
			"enabled",
			configs::bind_checkbox(std::format("{} enabled", id), rule.enabled),
			fonts::dejavu
		);
	}

	// what the panel shows while the config dropdown is open: the rules pointing at whichever config is
	// under the cursor, so they can be checked without selecting anything
	void rules_for_config(ui::Container& container, const std::string& config_name) {
		ui::add_text(
			"rules preview heading",
			container,
			std::format("rules for '{}'", config_name),
			gfx::Color::white(),
			fonts::dejavu
		);

		bool any = false;

		for (size_t i = 0; i < configs::rule_settings.rules.size(); i++) {
			const auto& rule = configs::rule_settings.rules[i];
			if (rule.config_name != config_name)
				continue;

			any = true;

			ui::add_text(
				std::format("rules preview {}", i),
				container,
				rule.enabled ? rule.pattern : std::format("{} (disabled)", rule.pattern),
				gfx::Color::white(rule.enabled ? 200 : gui::renderer::MUTED_SHADE),
				fonts::dejavu
			);
		}

		if (!any) {
			ui::add_text(
				"rules preview empty",
				container,
				"No rules send videos to this config.",
				gfx::Color::white(gui::renderer::MUTED_SHADE),
				fonts::dejavu(fonts::size::SMALL)
			);
		}

		ui::add_spacing(container, 8);

		ui::add_text(
			"rules preview hint",
			container,
			"Open the rules tab to edit them.",
			gfx::Color::white(gui::renderer::MUTED_SHADE),
			fonts::dejavu(fonts::size::SMALL)
		);
	}
}

void configs::rules(ui::Container& container) {
	if (temp_tab_owner == CONFIG_DROPDOWN_OWNER) {
		rules_for_config(container, hovered_config.empty() ? selected_config_name : hovered_config);
		return;
	}

	ui::add_text(
		"rules explanation",
		container,
		std::vector<std::string>{
			"Videos are matched against these rules when they're added.",
			"The first enabled rule that matches wins.",
		},
		gfx::Color::white(gui::renderer::MUTED_SHADE),
		fonts::dejavu(fonts::size::SMALL)
	);

	ui::add_spacing(container, 4);

	for (size_t i = 0; i < rule_settings.rules.size(); i++) {
		if (i > 0)
			ui::add_separator(std::format("rule {} separator", i), container, ui::SeparatorStyle::FADE_RIGHT);

		rule_row(container, i);

		ui::add_spacing(container, 8);
	}

	if (rule_settings.rules.empty()) {
		ui::add_text(
			"no rules text", container, "no rules yet", gfx::Color::white(gui::renderer::MUTED_SHADE), fonts::dejavu
		);
	}

	ui::add_button("add rule button", container, "Add rule", fonts::dejavu, [] {
		rule_settings.rules.push_back(
			ConfigRule{
				.config_name = selected_config_name,
			}
		);
	});
}
