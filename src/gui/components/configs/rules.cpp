#include "configs.h"

#include "../../ui/ui.h"
#include "../../ui/keys.h"
#include "../../renderer.h"

namespace configs = gui::components::configs;

namespace {
	constexpr int ROW_GAP = 4;

	// which rule the mouse is carrying, if any. the rows move around under the cursor as it goes, so the
	// drag is tracked here rather than by the handle that started it
	std::optional<size_t> dragging_rule;

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

	struct RowResult {
		ui::AnimatedElement* handle;
		int y;
		int y2;
	};

	RowResult rule_row(ui::Container& container, size_t index) {
		auto& rule = configs::rule_settings.rules[index];

		std::string id = std::format("rule {}", index);

		int icon_size = ui::text_input_height(fonts::dejavu);
		auto icon_dimensions = gfx::Size(icon_size, icon_size);

		int row_y = container.current_position.y;

		container.push_element_gap(ROW_GAP);

		auto* handle =
			ui::add_drag_handle(std::format("{} drag handle", id), container, icon_dimensions, "Drag to reorder");

		ui::set_next_same_line(container);

		ui::add_checkbox(
			std::format("{} enabled checkbox", id),
			container,
			"enabled",
			configs::bind_checkbox(std::format("{} enabled", id), rule.enabled),
			fonts::dejavu,
			{},
			true
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
			int full_width = container.get_usable_rect().w;
			int config_width = std::max(full_width / 2, 100);
			int pattern_width = full_width - config_width - configs::DELETE_ICON_GAP;

			ui::add_text_input(
				std::format("{} pattern input", id),
				container,
				configs::bind_input(std::format("{} pattern", id), rule.pattern),
				"",
				fonts::dejavu,
				"pattern",
				{},
				false,
				pattern_width
			);

			ui::set_next_same_line(container);

			container.push_usable_width((float)config_width / (float)full_width);

			auto* config_dropdown = ui::add_dropdown(
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

			ui::right_align_element(container, config_dropdown);
		});

		container.pop_element_gap();

		return RowResult{ .handle = handle, .y = row_y, .y2 = container.current_position.y };
	}

	// while a handle is held the rule follows the cursor into whichever row it's over. moving it rather
	// than swapping keeps the rest of the list in order however far it's dragged
	void update_drag(const std::vector<RowResult>& rows) {
		auto& rules = configs::rule_settings.rules;

		for (size_t i = 0; i < rows.size(); i++) {
			const auto& handle_data = std::get<ui::DragHandleElementData>(rows[i].handle->element->data);
			if (handle_data.pressed)
				dragging_rule = i;
		}

		if (!dragging_rule)
			return;

		if (!keys::is_mouse_dragging() || *dragging_rule >= rows.size()) {
			dragging_rule.reset();
			return;
		}

		for (size_t i = 0; i < rows.size(); i++) {
			if (i == *dragging_rule || keys::mouse_pos.y < rows[i].y || keys::mouse_pos.y >= rows[i].y2)
				continue;

			auto rule = rules[*dragging_rule];
			rules.erase(rules.begin() + *dragging_rule);
			rules.insert(rules.begin() + i, rule);

			dragging_rule = i;
			break;
		}

		// the rows shuffle underneath, so hand the grab to whichever handle the rule is now under
		ui::set_active_element(*rows[*dragging_rule].handle, "drag handle");
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
	if (temp_tab_owner == CONFIG_DROPDOWN_ID) {
		dragging_rule.reset();
		rules_for_config(container, hovered_config.empty() ? selected_config_name : hovered_config);
		return;
	}

	ui::add_text(
		"rules explanation",
		container,
		std::vector<std::string>{
			"Videos are matched against these rules as",
			"they are added - the first match wins.",
		},
		gfx::Color::white(gui::renderer::MUTED_SHADE),
		fonts::dejavu(fonts::size::SMALL)
	);

	ui::add_spacing(container, 6);

	std::vector<RowResult> rows;
	rows.reserve(rule_settings.rules.size());

	for (size_t i = 0; i < rule_settings.rules.size(); i++) {
		if (i > 0)
			ui::add_separator(std::format("rule {} separator", i), container, ui::SeparatorStyle::FADE_RIGHT);

		rows.push_back(rule_row(container, i));
	}

	update_drag(rows);

	if (rule_settings.rules.empty()) {
		ui::add_text(
			"no rules text", container, "no rules yet", gfx::Color::white(gui::renderer::MUTED_SHADE), fonts::dejavu
		);
	}

	ui::add_spacing(container, 8);

	ui::add_button("add rule button", container, "Add rule", fonts::dejavu, [] {
		rule_settings.rules.push_back(
			ConfigRule{
				.config_name = selected_config_name,
			}
		);
	});
}
