#include "../ui.h"
#include "../../render/render.h"
#include "../../fonts/icons.h"

#include "../keys.h"

const float DROPDOWN_ROUNDING = 5.f;
const gfx::Size DROPDOWN_PADDING(10, 5);
const float OPTION_LINE_HEIGHT_ADD = 11;
const int LABEL_GAP = 10;
const int OPTIONS_GAP = 3;
const gfx::Size OPTIONS_PADDING(10, 3);
const int DROPDOWN_ARROW_PAD = 2;
const gfx::Size OPTION_ACTION_SIZE(19, 19);
const int OPTION_ACTION_GAP = 1;
const int TEXT_ICON_GAP = 5;

namespace {
	struct Positions {
		gfx::Point label_pos;
		gfx::Rect dropdown_rect;
		gfx::Point selected_text_pos;
		gfx::Rect options_rect;
		float option_line_height{};
		size_t row_count{};
	};

	size_t get_row_count(const ui::DropdownElementData& dropdown_data) {
		return dropdown_data.options.size() + (dropdown_data.add_action ? 1 : 0);
	}

	Positions get_positions(
		const ui::Container& container,
		const ui::AnimatedElement& element,
		const ui::DropdownElementData& dropdown_data,
		float anim = 1.f
	) {
		gfx::Point label_pos = element.element->rect.origin();

		gfx::Rect dropdown_rect = element.element->rect;

		if (!dropdown_data.label.empty()) {
			dropdown_rect.y = label_pos.y + dropdown_data.font.height() + LABEL_GAP;
			dropdown_rect.h -= dropdown_rect.y - element.element->rect.y;
		}

		gfx::Point selected_text_pos = dropdown_rect.origin();
		selected_text_pos.x += DROPDOWN_PADDING.w;
		selected_text_pos.y = dropdown_rect.center().y;

		float option_line_height = dropdown_data.font.height() + OPTION_LINE_HEIGHT_ADD;
		size_t row_count = get_row_count(dropdown_data);

		gfx::Rect options_rect = element.element->rect;
		options_rect.y = options_rect.y2() + OPTIONS_GAP;
		options_rect.h = option_line_height * row_count + OPTIONS_PADDING.h * 2;

		if (options_rect.y + options_rect.h + OPTIONS_GAP > container.get_usable_rect().y2() &&
		    options_rect.y - options_rect.h - OPTIONS_GAP > container.get_usable_rect().y)
		{
			// open upwards
			options_rect.h *= anim;
			options_rect.y = dropdown_rect.y - options_rect.h - OPTIONS_GAP;
		}
		else {
			options_rect.h *= anim;
		}

		return {
			.label_pos = label_pos,
			.dropdown_rect = dropdown_rect,
			.selected_text_pos = selected_text_pos,
			.options_rect = options_rect,
			.option_line_height = option_line_height,
			.row_count = row_count,
		};
	}

	gfx::Rect get_row_rect(const Positions& pos, size_t row) {
		return {
			pos.options_rect.x,
			(int)(pos.options_rect.y + OPTIONS_PADDING.h + (row * pos.option_line_height)),
			pos.options_rect.w,
			(int)pos.option_line_height,
		};
	}

	std::vector<size_t> get_row_actions(const ui::DropdownElementData& dropdown_data, const std::string& option) {
		std::vector<size_t> indices;

		for (size_t i = 0; i < dropdown_data.option_actions.size(); i++) {
			const auto& action = dropdown_data.option_actions[i];
			if (action.applies_to && !action.applies_to(option))
				continue;

			indices.push_back(i);
		}

		return indices;
	}

	gfx::Rect get_action_rect(const Positions& pos, size_t row, size_t slot, size_t slot_count) {
		gfx::Rect row_rect = get_row_rect(pos, row);

		int right = row_rect.x2() - OPTIONS_PADDING.w -
		            (int)((slot_count - 1 - slot) * (OPTION_ACTION_SIZE.w + OPTION_ACTION_GAP));

		return {
			right - OPTION_ACTION_SIZE.w,
			row_rect.center().y - (OPTION_ACTION_SIZE.h / 2),
			OPTION_ACTION_SIZE.w,
			OPTION_ACTION_SIZE.h,
		};
	}

	int get_selected_text_width(const Positions& pos) {
		int arrow_width = fonts::icons.calc_size(icons::DROPDOWN_ARROW).w;

		return pos.dropdown_rect.w - (DROPDOWN_PADDING.w * 2) - DROPDOWN_ARROW_PAD - (arrow_width / 2) - TEXT_ICON_GAP;
	}

	int get_option_text_width(const Positions& pos, size_t action_count) {
		int actions_width = (int)action_count * (OPTION_ACTION_SIZE.w + OPTION_ACTION_GAP);

		return pos.options_rect.w - (OPTIONS_PADDING.w * 2) - actions_width - (action_count > 0 ? TEXT_ICON_GAP : 0);
	}

	size_t get_option_hover_key(size_t option_index) {
		return ui::hasher("option_hover_" + std::to_string(option_index));
	}

	size_t get_action_hover_key(size_t option_index, size_t action_index) {
		return ui::hasher(std::format("action_hover_{}_{}", option_index, action_index));
	}

	size_t get_add_row_hover_key() {
		return ui::hasher("add_row_hover");
	}

	ui::AnimationState& get_hover_animation(ui::AnimatedElement& element, size_t key) {
		auto [it, inserted] = element.animations.try_emplace(key, 80.f);
		return it->second;
	}

	float get_hover_animation_value(const ui::AnimatedElement& element, size_t key) {
		auto it = element.animations.find(key);
		if (it != element.animations.end()) {
			return it->second.current;
		}
		return 0.0f;
	}
}

void ui::render_dropdown(const Container& container, const AnimatedElement& element) {
	const auto& dropdown_data = std::get<DropdownElementData>(element.element->data);

	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;
	float expand_anim = element.animations.at(hasher("expand")).current;
	float expand_goal = element.animations.at(hasher("expand")).goal;

	auto pos = get_positions(container, element, dropdown_data, expand_anim);

	// Background color
	int background_shade = 15 + (10 * hover_anim);
	int border_shade = 70;

	gfx::Color adjusted_color(background_shade, background_shade, background_shade, anim * 255);
	gfx::Color text_color(255, 255, 255, anim * 255);
	gfx::Color muted_text_color(255, 255, 255, anim * 100);
	gfx::Color selected_text_color(255, 100, 100, anim * 255);
	gfx::Color muted_selected_text_color(255, 100, 100, anim * 120);
	gfx::Color hover_text_color(255, 150, 150, anim * 255);
	gfx::Color border_color(border_shade, border_shade, border_shade, anim * 255);
	gfx::Color arrow_colour(100, 100, 100, anim * 255);
	gfx::Color action_colour(255, 255, 255, anim * 110);

	auto is_muted = [&](const std::string& option) {
		return u::contains(dropdown_data.muted_options, option);
	};

	if (!dropdown_data.label.empty())
		render::text(pos.label_pos, text_color, dropdown_data.label, dropdown_data.font);

	// Render dropdown main area
	render::rounded_rect_filled(pos.dropdown_rect, adjusted_color, DROPDOWN_ROUNDING);
	render::rounded_rect_stroke(pos.dropdown_rect, border_color, DROPDOWN_ROUNDING);

	// Get currently selected option text
	std::string selected_text = *dropdown_data.selected;
	render::clip_string(selected_text, dropdown_data.font, get_selected_text_width(pos));

	render::text(
		pos.selected_text_pos,
		is_muted(*dropdown_data.selected) ? muted_text_color : text_color,
		selected_text,
		dropdown_data.font,
		FONT_CENTERED_Y
	);

	// Render dropdown arrow
	gfx::Point arrow_pos(
		pos.dropdown_rect.x2() - DROPDOWN_PADDING.w - DROPDOWN_ARROW_PAD, pos.dropdown_rect.center().y
	);

	// const float arrow_length = 4.8f;
	// const float arrow_angle_rad = u::deg_to_rad(46.f);

	// int arrow_offset_x = round(arrow_length * std::cos(arrow_angle_rad));
	// int arrow_offset_y = round(arrow_length * std::sin(arrow_angle_rad));

	// std::vector<gfx::Point> arrow_points = {
	// 	{ arrow_pos.x - arrow_offset_x, arrow_pos.y - arrow_offset_y },
	// 	arrow_pos,
	// 	{ arrow_pos.x + arrow_offset_x, arrow_pos.y - arrow_offset_y },
	// };

	// for (int i = 1; i < arrow_points.size(); i++) {
	// 	render::line(arrow_points[i - 1], arrow_points[i], arrow_colour, true);
	// }

	// todo: draw this manually rather than using icon via font but font rendering looks way nicer than lines
	render::text(
		arrow_pos,
		arrow_colour,
		icons::DROPDOWN_ARROW,
		fonts::icons,
		FONT_CENTERED_X | FONT_CENTERED_Y,
		expand_goal * 180.f
	);

	// Render dropdown options
	if (expand_anim > 0.01f) {
		gfx::Color option_color(7, 7, 7, anim * 255);
		gfx::Color option_border_color(border_shade, border_shade, border_shade, anim * 255);

		render::late_draw_calls.emplace_back([pos, option_color, option_border_color] {
			render::rounded_rect_filled(pos.options_rect, option_color, DROPDOWN_ROUNDING);
			render::rounded_rect_stroke(pos.options_rect, option_border_color, DROPDOWN_ROUNDING);

			render::push_clip_rect(pos.options_rect);
		});

		// Render options
		gfx::Point option_text_pos = pos.options_rect.origin();
		option_text_pos.y += OPTIONS_PADDING.h + OPTION_LINE_HEIGHT_ADD / 2 - 1;
		option_text_pos.x = pos.options_rect.origin().x + OPTIONS_PADDING.w;

		for (size_t i = 0; i < dropdown_data.options.size(); i++) {
			const auto& option = dropdown_data.options[i];
			bool selected = option == *dropdown_data.selected;
			float option_hover_anim = get_hover_animation_value(element, get_option_hover_key(i));

			gfx::Color option_base_colour = is_muted(option) ? muted_text_color : text_color;
			gfx::Color option_selected_colour = is_muted(option) ? muted_selected_text_color : selected_text_color;

			gfx::Color option_text_colour =
				selected ? option_selected_colour
						 : gfx::Color::lerp(option_base_colour, hover_text_color, option_hover_anim);

			auto row_actions = get_row_actions(dropdown_data, option);

			std::string option_text = option;
			render::clip_string(option_text, dropdown_data.font, get_option_text_width(pos, row_actions.size()));

			render::late_draw_calls.emplace_back(
				[option_text_pos, option_text_colour, option_text, font = dropdown_data.font] {
					render::text(option_text_pos, option_text_colour, option_text, font);
				}
			);

			for (size_t slot = 0; slot < row_actions.size(); slot++) {
				size_t action_index = row_actions[slot];
				const auto& action = dropdown_data.option_actions[action_index];

				gfx::Rect action_rect = get_action_rect(pos, i, slot, row_actions.size());

				gfx::Color icon_base_colour = action.color ? action.color->adjust_alpha(anim * 255) : action_colour;

				gfx::Color icon_colour = gfx::Color::lerp(
					icon_base_colour,
					action.hover_color.adjust_alpha(anim * 255),
					get_hover_animation_value(element, get_action_hover_key(i, action_index))
				);

				render::late_draw_calls.emplace_back([action_rect, icon_colour, icon = action.icon] {
					render::text(
						action_rect.center(), icon_colour, icon, fonts::icons, FONT_CENTERED_X | FONT_CENTERED_Y
					);
				});
			}

			option_text_pos.y += pos.option_line_height;
		}

		if (dropdown_data.add_action) {
			gfx::Rect add_row_rect = get_row_rect(pos, dropdown_data.options.size());

			gfx::Color add_colour = gfx::Color::lerp(
				action_colour, text_color, get_hover_animation_value(element, get_add_row_hover_key())
			);

			render::late_draw_calls.emplace_back([add_row_rect, add_colour] {
				render::text(
					add_row_rect.center(), add_colour, icons::ADD, fonts::icons, FONT_CENTERED_X | FONT_CENTERED_Y
				);
			});
		}

		render::late_draw_calls.emplace_back([] {
			render::pop_clip_rect();
		});
	}
}

bool ui::update_dropdown(const Container& container, AnimatedElement& element) {
	auto& dropdown_data = std::get<DropdownElementData>(element.element->data);

	auto& hover_anim = element.animations.at(hasher("hover"));
	auto& expand_anim = element.animations.at(hasher("expand"));

	auto pos = get_positions(container, element, dropdown_data, expand_anim.current);

	bool hovered = pos.dropdown_rect.contains(keys::mouse_pos) && set_hovered_element(element);
	hover_anim.set_goal(hovered ? 1.f : 0.f);

	bool active = get_active_element() == &element;

	auto toggle_active = [&] {
		if (active) {
			reset_active_element();
			active = false;
		}
		else {
			set_active_element(element);
			active = true;
		}

		expand_anim.set_goal(active ? 1.f : 0.f);
	};

	bool activated = false;

	if (hovered) {
		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

		if (keys::is_mouse_down()) {
			// toggle dropdown
			toggle_active();
			keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

			activated = true;
		}
	}

	dropdown_data.hovered_option = "";

	size_t hovered_row = -1;
	size_t hovered_action = -1; // indexes option_actions, not the slot it's drawn in

	if (!activated && active) {
		if (pos.options_rect.contains(keys::mouse_pos)) {
			// prevent elements behind the open list from receiving hover tooltips
			set_hovered_element(element);
			set_cursor(SDL_SYSTEM_CURSOR_POINTER);

			int y_offset = keys::mouse_pos.y - pos.options_rect.y - OPTIONS_PADDING.h;

			if (y_offset >= 0) {
				hovered_row = y_offset / pos.option_line_height;
				if (hovered_row >= pos.row_count) {
					hovered_row = -1;
				}
			}

			if (hovered_row != -1 && hovered_row < dropdown_data.options.size()) {
				dropdown_data.hovered_option = dropdown_data.options[hovered_row];

				auto row_actions = get_row_actions(dropdown_data, dropdown_data.options[hovered_row]);

				for (size_t slot = 0; slot < row_actions.size(); slot++) {
					if (!get_action_rect(pos, hovered_row, slot, row_actions.size()).contains(keys::mouse_pos))
						continue;

					const auto& action = dropdown_data.option_actions[row_actions[slot]];

					if (action.on_press)
						hovered_action = row_actions[slot];

					if (!action.tooltip.empty())
						tooltip::set(action.tooltip);

					break;
				}
			}
			else if (hovered_row != -1 && dropdown_data.add_action) {
				if (!dropdown_data.add_action->tooltip.empty())
					tooltip::set(dropdown_data.add_action->tooltip);
			}
		}

		if (keys::is_mouse_down()) {
			if (pos.options_rect.contains(keys::mouse_pos)) {
				// eat all inputs cause otherwise itll click stuff behind
				keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

				// the callbacks rebuild the options, so take a copy of what's being called and close the
				// dropdown before running it
				if (hovered_action != -1) {
					auto on_press = dropdown_data.option_actions[hovered_action].on_press;
					std::string option = dropdown_data.options[hovered_row];

					toggle_active();

					if (on_press)
						on_press(option);

					activated = true;
				}
				else if (dropdown_data.add_action && hovered_row == dropdown_data.options.size()) {
					auto on_press = dropdown_data.add_action->on_press;

					toggle_active();

					if (on_press)
						on_press();

					activated = true;
				}
				else if (hovered_row != -1) {
					std::string new_selected = dropdown_data.options[hovered_row];
					if (new_selected != *dropdown_data.selected) {
						*dropdown_data.selected = new_selected;
						toggle_active();

						if (dropdown_data.on_change)
							(*dropdown_data.on_change)(dropdown_data.selected);

						activated = true;
					}
				}
			}
			else {
				toggle_active();
			}
		}
	}

	for (size_t i = 0; i < dropdown_data.options.size(); i++) {
		bool row_hovered = active && i == hovered_row;

		get_hover_animation(element, get_option_hover_key(i)).set_goal(row_hovered ? 1.f : 0.f);

		for (size_t action_index = 0; action_index < dropdown_data.option_actions.size(); action_index++) {
			get_hover_animation(element, get_action_hover_key(i, action_index))
				.set_goal(row_hovered && action_index == hovered_action ? 1.f : 0.f);
		}
	}

	if (dropdown_data.add_action) {
		bool add_row_hovered = active && hovered_row == dropdown_data.options.size();
		get_hover_animation(element, get_add_row_hover_key()).set_goal(add_row_hovered ? 1.f : 0.f);
	}

	// update z index
	int z_index = 0;
	if (active)
		z_index = 2;
	else if (expand_anim.current > 0.01f)
		z_index = 1;
	element.z_index = z_index;

	return activated;
}

ui::AnimatedElement* ui::add_dropdown(
	const std::string& id,
	Container& container,
	const std::string& label,
	const std::vector<std::string>& options,
	std::string& selected,
	const render::Font& font,
	std::optional<std::function<void(std::string*)>> on_change,
	const std::vector<std::string>& muted_options,
	const std::vector<DropdownOptionAction>& option_actions,
	std::optional<DropdownAddAction> add_action
) {
	// gfx::Size max_text_size(0, font.getSize());

	// // Find max text size for width calculation
	// for (const auto& option : options) {
	// 	gfx::Size text_size = render::get_text_size(option, font);
	// 	max_text_size.w = std::max(max_text_size.w, text_size.w);
	// }

	int height = font.height() + (DROPDOWN_PADDING.h * 2);
	if (!label.empty())
		height += font.height() + LABEL_GAP;

	gfx::Size total_size(container.get_usable_rect().w, height);

	Element element(
		id,
		ElementType::DROPDOWN,
		gfx::Rect(container.current_position, total_size),
		DropdownElementData{
			.label = label,
			.options = options,
			.selected = &selected,
			.font = font,
			.on_change = std::move(on_change),
			.muted_options = muted_options,
			.option_actions = option_actions,
			.add_action = std::move(add_action),
			.hovered_option = "",
		},
		render_dropdown,
		update_dropdown
	);

	return add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(25.f) },
			{ hasher("hover"), AnimationState(80.f) },
			{ hasher("expand"), AnimationState(30.f) },
		}
	);
}
