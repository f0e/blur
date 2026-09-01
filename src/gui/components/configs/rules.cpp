#include "configs.h"

#include "../../ui/ui.h"
#include "../../ui/keys.h"
#include "../../renderer.h"

namespace configs = gui::components::configs;

namespace {
	constexpr int ROW_GAP = 4;

	constexpr float ROW_SETTLE_SPEED = 50.f;
	constexpr float ROW_SETTLE_SNAP = 0.5f;

	constexpr int LIFT_PADDING_X = 6;
	constexpr int LIFT_PADDING_Y = 5;

	constexpr int LIFT_Z_INDEX = 10;

	constexpr int AUTO_SCROLL_EDGE = 40;
	constexpr float AUTO_SCROLL_SPEED = 800.f;

	// keep element animations with their rows
	constexpr std::array ANIMATED_ROW_ELEMENTS = { "drag handle" };

	struct Drag {
		size_t index = 0;
		int grab_offset = 0;
	};

	std::optional<Drag> drag;

	struct RowState {
		float offset = 0.f;
		int y = 0;
		int h = 0;
		bool laid_out = false;
	};

	std::vector<RowState> row_states;

	std::vector<std::string> lifted_element_ids;

	size_t last_built_frame = 0;

	struct Row {
		int y = 0;
		int h = 0;
		ui::AnimatedElement* handle = nullptr;
		std::vector<ui::AnimatedElement*> elements;
	};

	std::string row_element_id(size_t index, std::string_view suffix) {
		return std::format("rule {} {}", index, suffix);
	}

	ui::AnimatedElement* find_row_element(ui::Container& container, size_t index, std::string_view suffix) {
		auto it = container.elements.find(row_element_id(index, suffix));
		return it == container.elements.end() ? nullptr : &it->second;
	}

	void swap_row_animations(ui::Container& container, size_t a, size_t b) {
		for (std::string_view suffix : ANIMATED_ROW_ELEMENTS) {
			auto* first = find_row_element(container, a, suffix);
			auto* second = find_row_element(container, b, suffix);

			if (first && second)
				std::swap(first->animations, second->animations);
		}
	}

	void stop_drag(ui::Container& container) {
		drag.reset();

		for (const auto& id : lifted_element_ids) {
			auto it = container.elements.find(id);
			if (it != container.elements.end())
				it->second.z_index = 0;
		}

		lifted_element_ids.clear();
	}

	int to_row_space(const ui::Container& container, int y) {
		return y + std::lround(container.scroll_y);
	}

	int dragged_row_y(const ui::Container& container) {
		return to_row_space(container, keys::mouse_pos.y) - drag->grab_offset;
	}

	int row_middle(size_t index) {
		return row_states[index].y + (row_states[index].h / 2);
	}

	float get_max_scroll(const ui::Container& container) {
		auto usable_rect = container.get_usable_rect();

		int content_bottom = usable_rect.y;
		for (const auto& [id, element] : container.elements)
			content_bottom = std::max(content_bottom, element.element->orig_rect.y2());

		return std::max((float)(content_bottom - usable_rect.y2()), 0.f);
	}

	void auto_scroll(ui::Container& container, float delta_time) {
		auto usable_rect = container.get_usable_rect();

		float speed = 0.f;
		if (keys::mouse_pos.y < usable_rect.y + AUTO_SCROLL_EDGE)
			speed = -(float)(usable_rect.y + AUTO_SCROLL_EDGE - keys::mouse_pos.y) / AUTO_SCROLL_EDGE;
		else if (keys::mouse_pos.y > usable_rect.y2() - AUTO_SCROLL_EDGE)
			speed = (float)(keys::mouse_pos.y - (usable_rect.y2() - AUTO_SCROLL_EDGE)) / AUTO_SCROLL_EDGE;

		if (speed == 0.f)
			return;

		container.scroll_y = std::clamp(
			container.scroll_y + (std::clamp(speed, -1.f, 1.f) * AUTO_SCROLL_SPEED * delta_time),
			0.f,
			get_max_scroll(container)
		);

		container.scroll_to_top = false;
		container.scroll_speed_y = 0.f;
	}

	void update_drag(ui::Container& container, float delta_time) {
		auto& rules = configs::rule_settings.rules;

		if (!drag) {
			for (size_t i = 0; i < rules.size(); i++) {
				auto* handle = find_row_element(container, i, "drag handle");
				if (!handle || !ui::is_active_element(*handle, "drag handle") || !row_states[i].laid_out)
					continue;

				drag = Drag{
					.index = i,
					.grab_offset = to_row_space(container, keys::mouse_pos.y) -
					               (row_states[i].y + std::lround(row_states[i].offset)),
				};

				break;
			}
		}

		if (!drag)
			return;

		if (drag->index >= rules.size() || !keys::is_mouse_dragging()) {
			stop_drag(container);
			return;
		}

		auto_scroll(container, delta_time);

		int middle = dragged_row_y(container) + (row_states[drag->index].h / 2);

		std::optional<size_t> swap_with;
		if (drag->index + 1 < rules.size() && middle > row_middle(drag->index + 1))
			swap_with = drag->index + 1;
		else if (drag->index > 0 && middle < row_middle(drag->index - 1))
			swap_with = drag->index - 1;

		if (swap_with) {
			std::swap(rules[drag->index], rules[*swap_with]);
			std::swap(row_states[drag->index], row_states[*swap_with]);
			swap_row_animations(container, drag->index, *swap_with);

			drag->index = *swap_with;
		}

		if (auto* handle = find_row_element(container, drag->index, "drag handle"))
			ui::set_active_element(*handle, "drag handle");
	}

	void animate_rows(ui::Container& container, const std::vector<Row>& rows, float delta_time) {
		auto usable_rect = container.get_usable_rect();

		std::vector<std::string> lifted;

		for (size_t i = 0; i < rows.size(); i++) {
			const Row& row = rows[i];
			auto& state = row_states[i];

			bool dragged = drag && drag->index == i;

			if (dragged) {
				int lowest = rows.back().y + rows.back().h - row.h;
				int y = std::clamp(dragged_row_y(container), rows.front().y, std::max(lowest, rows.front().y));

				state.offset = (float)(y - row.y);
			}
			else if (state.laid_out) {
				state.offset += (float)(state.y - row.y);
				state.offset = u::lerp(state.offset, 0.f, ROW_SETTLE_SPEED * delta_time, ROW_SETTLE_SNAP);
			}
			else {
				state.offset = 0.f;
			}

			state.y = row.y;
			state.h = row.h;
			state.laid_out = true;

			int offset = std::lround(state.offset);

			for (auto* element : row.elements) {
				element->element->rect.y += offset;
				element->element->orig_rect.y += offset;
			}

			auto& lift_anim = row.handle->animations.at(ui::hasher("lift"));
			lift_anim.set_goal(dragged ? 1.f : 0.f);

			bool lifted_row = dragged || lift_anim.current > 0.f;

			auto& handle_data = std::get<ui::DragHandleElementData>(row.handle->element->data);
			handle_data.row_rect = lifted_row ? gfx::Rect(
													usable_rect.x - LIFT_PADDING_X,
													row.y + offset - LIFT_PADDING_Y,
													usable_rect.w + (LIFT_PADDING_X * 2),
													row.h + (LIFT_PADDING_Y * 2)
												)
			                                  : gfx::Rect();
			handle_data.row_anchor_y = row.handle->element->rect.y;

			if (lifted_row) {
				for (auto* element : row.elements) {
					element->z_index = LIFT_Z_INDEX;
					lifted.push_back(element->element->id);
				}

				row.handle->z_index = LIFT_Z_INDEX - 1;
			}

			// keep the list redrawing while rows move
			if (offset != 0 || lifted_row)
				row.handle->element->always_render = true;
		}

		for (const auto& id : lifted_element_ids) {
			if (u::contains(lifted, id))
				continue;

			auto it = container.elements.find(id);
			if (it != container.elements.end())
				it->second.z_index = 0;
		}

		lifted_element_ids = std::move(lifted);
	}

	std::vector<std::string> config_options(const std::string& current) {
		std::vector<std::string> names;
		names.reserve(configs::edited_configs.size() + 1);

		for (const auto& [name, config] : configs::edited_configs) {
			names.push_back(name);
		}

		// show missing configs in the dropdown
		if (!current.empty() && !u::contains(names, current))
			names.push_back(current);

		return names;
	}

	bool config_missing(const std::string& name) {
		return !name.empty() && !configs::edited_configs.contains(name);
	}

	Row rule_row(ui::Container& container, size_t index) {
		auto& rule = configs::rule_settings.rules[index];

		int icon_size = ui::text_input_height(fonts::dejavu);
		auto icon_dimensions = gfx::Size(icon_size, icon_size);

		size_t first_element = container.current_element_ids.size();

		container.push_element_gap(ROW_GAP);

		std::optional<std::string> message;
		gfx::Color message_color = configs::WARNING_COLOR;

		if (config_missing(rule.config_name)) {
			message = std::format("rule disabled - '{}' no longer exists", rule.config_name);
			message_color = configs::ERROR_COLOR;
		}
		else if (u::trim(rule.pattern).empty()) {
			message = "enter a pattern for this rule to match";
		}

		ui::AnimatedElement* handle = nullptr;

		ui::add_with_message(container, row_element_id(index, "message"), message, message_color, [&] {
			int full_width = container.get_usable_rect().w;
			int fields_width = full_width - (icon_size * 2) - (container.element_gap * 3);
			int config_width = std::max(int(fields_width * 0.45f), 100);
			int pattern_width = fields_width - config_width;

			handle = ui::add_drag_handle(
				row_element_id(index, "drag handle"), container, icon_dimensions, "Drag to reorder"
			);

			ui::set_next_same_line(container);

			ui::add_text_input(
				row_element_id(index, "pattern input"),
				container,
				configs::bind_input(std::format("rule {} pattern", index), rule.pattern),
				"",
				fonts::dejavu,
				"pattern",
				{},
				false,
				pattern_width
			);

			ui::set_next_same_line(container);

			int config_x = container.current_position.x;
			container.push_usable_width((float)config_width / (float)full_width);
			container.current_position.x = config_x;

			ui::add_dropdown(
				row_element_id(index, "config dropdown"),
				container,
				"",
				config_options(rule.config_name),
				configs::bind_read_only_input(std::format("rule {} config", index), rule.config_name),
				fonts::dejavu,
				[index](std::string* new_value) {
					if (index < configs::rule_settings.rules.size())
						configs::rule_settings.rules[index].config_name = *new_value;
				},
				config_missing(rule.config_name) ? std::vector{ rule.config_name } : std::vector<std::string>{}
			);

			container.pop_usable_width();

			ui::set_next_same_line(container);

			auto* delete_button = ui::add_icon_button(
				row_element_id(index, "delete button"),
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
		});

		container.pop_element_gap();

		Row row{ .handle = handle };

		int row_y2 = 0;

		for (size_t i = first_element; i < container.current_element_ids.size(); i++) {
			auto it = container.elements.find(container.current_element_ids[i]);
			if (it == container.elements.end())
				continue;

			const auto& rect = it->second.element->rect;

			row.y = row.elements.empty() ? rect.y : std::min(row.y, rect.y);
			row_y2 = row.elements.empty() ? rect.y2() : std::max(row_y2, rect.y2());

			row.elements.push_back(&it->second);
		}

		row.h = row_y2 - row.y;

		return row;
	}

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
				std::format("rules preview {}", i), container, rule.pattern, gfx::Color::white(200), fonts::dejavu
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

void configs::rules(ui::Container& container, float delta_time) {
	bool drawn_last_frame = ui::frame == last_built_frame + 1;
	last_built_frame = ui::frame;

	if (temp_tab_owner == CONFIG_DROPDOWN_ID) {
		stop_drag(container);
		row_states.clear();

		rules_for_config(container, hovered_config.empty() ? selected_config_name : hovered_config);
		return;
	}

	if (!drawn_last_frame || row_states.size() != rule_settings.rules.size()) {
		stop_drag(container);
		row_states.assign(rule_settings.rules.size(), RowState{});
	}

	update_drag(container, delta_time);

	std::vector<Row> rows;
	rows.reserve(rule_settings.rules.size());

	for (size_t i = 0; i < rule_settings.rules.size(); i++) {
		rows.push_back(rule_row(container, i));
	}

	animate_rows(container, rows, delta_time);

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
