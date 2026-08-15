#include "../ui.h"
#include "../../render/render.h"

#include "../keys.h"

const float DROPDOWN_ROUNDING = 5.f;
const gfx::Size DROPDOWN_PADDING(10, 5);
const float OPTION_LINE_HEIGHT_ADD = 11;
const int LABEL_GAP = 10;
const int OPTIONS_GAP = 3;
const gfx::Size OPTIONS_PADDING(10, 3);
// const float DROPDOWN_ARROW_SIZE = 10.0f;
const std::string DROPDOWN_ARROW_ICON = "a"; // dropdown arrow thing
const int DROPDOWN_ARROW_PAD = 2;

namespace {
	struct Positions {
		gfx::Point label_pos;
		gfx::Rect dropdown_rect;
		gfx::Point selected_text_pos;
		gfx::Rect options_rect;
		float option_line_height{};
	};

	Positions get_positions(
		const ui::Container& container,
		const ui::AnimatedElement& element,
		const ui::DropdownElementData& dropdown_data,
		float anim = 1.f
	) {
		gfx::Point label_pos = element.element->rect.origin();

		gfx::Rect dropdown_rect = element.element->rect;
		dropdown_rect.y = label_pos.y + dropdown_data.font->height() + LABEL_GAP;
		dropdown_rect.h -= dropdown_rect.y - element.element->rect.y;

		gfx::Point selected_text_pos = dropdown_rect.origin();
		selected_text_pos.x += DROPDOWN_PADDING.w;
		selected_text_pos.y = dropdown_rect.center().y;

		float option_line_height = dropdown_data.font->height() + OPTION_LINE_HEIGHT_ADD;

		gfx::Rect options_rect = element.element->rect;
		options_rect.y = options_rect.y2() + OPTIONS_GAP;
		options_rect.h = option_line_height * dropdown_data.options.size() + OPTIONS_PADDING.h * 2;

		if (options_rect.y + options_rect.h + OPTIONS_GAP > container.get_usable_rect().y2() &&
		    options_rect.y - options_rect.h - OPTIONS_GAP > 0)
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
		};
	}

	size_t get_option_hover_key(size_t option_index) {
		return ui::hasher("option_hover_" + std::to_string(option_index));
	}

	ui::AnimationState& get_hover_animation(ui::AnimatedElement& element, size_t index) {
		auto key = get_option_hover_key(index);
		auto [it, inserted] = element.animations.try_emplace(key, 80.f);
		return it->second;
	}

	float get_hover_animation_value(const ui::AnimatedElement& element, size_t index) {
		auto key = get_option_hover_key(index);
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

	auto is_muted = [&](const std::string& option) {
		return u::contains(dropdown_data.muted_options, option);
	};

	render::text(pos.label_pos, text_color, dropdown_data.label, *dropdown_data.font);

	// Render dropdown main area
	render::rounded_rect_filled(pos.dropdown_rect, adjusted_color, DROPDOWN_ROUNDING);
	render::rounded_rect_stroke(pos.dropdown_rect, border_color, DROPDOWN_ROUNDING);

	// Get currently selected option text
	render::text(
		pos.selected_text_pos,
		is_muted(*dropdown_data.selected) ? muted_text_color : text_color,
		*dropdown_data.selected,
		*dropdown_data.font,
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
		DROPDOWN_ARROW_ICON,
		fonts::icons,
		FONT_CENTERED_X | FONT_CENTERED_Y,
		expand_goal * 180.f,
		17 // hardcoded lol but its correct enough
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
			float option_hover_anim = get_hover_animation_value(element, i);

			gfx::Color option_base_colour = is_muted(option) ? muted_text_color : text_color;
			gfx::Color option_selected_colour = is_muted(option) ? muted_selected_text_color : selected_text_color;

			gfx::Color option_text_colour =
				selected ? option_selected_colour
						 : gfx::Color::lerp(option_base_colour, hover_text_color, option_hover_anim);

			render::late_draw_calls.emplace_back(
				[option_text_pos, option_text_colour, option, font = *dropdown_data.font] {
					render::text(option_text_pos, option_text_colour, option, font);
				}
			);

			option_text_pos.y += pos.option_line_height;
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

	if (!activated && active) {
		if (pos.options_rect.contains(keys::mouse_pos)) {
			set_cursor(SDL_SYSTEM_CURSOR_POINTER);

			// Calculate which option is being hovered
			int y_offset = keys::mouse_pos.y - pos.options_rect.y - OPTIONS_PADDING.h;
			size_t hovered_option_index = -1;

			if (y_offset >= 0) {
				hovered_option_index = y_offset / pos.option_line_height;
				if (hovered_option_index >= dropdown_data.options.size()) {
					hovered_option_index = -1;
				}
			}

			if (hovered_option_index != -1)
				dropdown_data.hovered_option = dropdown_data.options[hovered_option_index];

			// Update all option animations
			for (size_t i = 0; i < dropdown_data.options.size(); i++) {
				auto& anim = get_hover_animation(element, i);
				anim.set_goal(i == hovered_option_index ? 1.f : 0.f);
			}
		}
		else {
			// Reset all option hover animations when not over options area
			for (size_t i = 0; i < dropdown_data.options.size(); i++) {
				auto& anim = get_hover_animation(element, i);
				anim.set_goal(0.f);
			}
		}

		// clicking options
		if (keys::is_mouse_down()) {
			if (pos.options_rect.contains(keys::mouse_pos)) {
				size_t clicked_option_index =
					(keys::mouse_pos.y - pos.options_rect.y - OPTIONS_PADDING.h) / pos.option_line_height;

				// eat all inputs cause otherwise itll click stuff behind
				keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

				if (clicked_option_index >= 0 && clicked_option_index < dropdown_data.options.size()) {
					std::string new_selected = dropdown_data.options[clicked_option_index];
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
	else if (!active) {
		// Reset all option hover animations when dropdown is closed
		for (size_t i = 0; i < dropdown_data.options.size(); i++) {
			auto& anim = get_hover_animation(element, i);
			anim.set_goal(0.f);
		}
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
	const std::vector<std::string>& muted_options
) {
	// gfx::Size max_text_size(0, font.getSize());

	// // Find max text size for width calculation
	// for (const auto& option : options) {
	// 	gfx::Size text_size = render::get_text_size(option, font);
	// 	max_text_size.w = std::max(max_text_size.w, text_size.w);
	// }

	gfx::Size total_size(
		container.get_usable_rect().w, font.height() + LABEL_GAP + font.height() + (DROPDOWN_PADDING.h * 2)
	);

	Element element(
		id,
		ElementType::DROPDOWN,
		gfx::Rect(container.current_position, total_size),
		DropdownElementData{
			.label = label,
			.options = options,
			.selected = &selected,
			.font = &font,
			.on_change = std::move(on_change),
			.muted_options = muted_options,
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
