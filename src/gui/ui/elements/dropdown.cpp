#include <algorithm>
#include <utility>
#include <vector>

#include "../ui.h"
#include "../render.h"
#include "../utils.h"
#include "../keys.h"

#include "../../renderer.h"

const float DROPDOWN_ROUNDING = 5.f;
const gfx::Size DROPDOWN_PADDING(10, 5);
const float OPTION_LINE_HEIGHT_ADD = 11;
const float DROPDOWN_ARROW_SIZE = 10.0f;
const int LABEL_GAP = 10;
const int OPTIONS_GAP = 3;
const gfx::Size OPTIONS_PADDING(10, 3);

namespace {
	struct Positions {
		int font_height;
		gfx::Point label_pos;
		gfx::Rect dropdown_rect;
		gfx::Point selected_text_pos;
		gfx::Rect options_rect;
		float option_line_height;
	};

	Positions get_positions(
		const ui::Container& container,
		const ui::AnimatedElement& element,
		const ui::DropdownElementData& dropdown_data,
		float anim = 1.f
	) {
		int font_height = render::get_font_height(dropdown_data.font);

		gfx::Point label_pos = element.element->rect.origin();
		label_pos.y += font_height;

		gfx::Rect dropdown_rect = element.element->rect;
		dropdown_rect.y = label_pos.y + LABEL_GAP;
		dropdown_rect.h -= dropdown_rect.y - element.element->rect.y;

		gfx::Point selected_text_pos = dropdown_rect.origin();
		selected_text_pos.x += DROPDOWN_PADDING.w;
		selected_text_pos.y = dropdown_rect.center().y + font_height / 2 - 1;

		float option_line_height = dropdown_data.font.getSize() + OPTION_LINE_HEIGHT_ADD;

		gfx::Rect options_rect = element.element->rect;
		options_rect.y = options_rect.y2() + OPTIONS_GAP;
		options_rect.h = option_line_height * dropdown_data.options.size() + OPTIONS_PADDING.h * 2;

		// if it'll pass the end of the container open upwards
		if (options_rect.y2() > container.rect.y2()) {
			options_rect.h *= anim;
			options_rect.y = dropdown_rect.y - options_rect.h - OPTIONS_GAP;
		}
		else {
			options_rect.h *= anim;
		}

		return {
			.font_height = font_height,
			.label_pos = label_pos,
			.dropdown_rect = dropdown_rect,
			.selected_text_pos = selected_text_pos,
			.options_rect = options_rect,
			.option_line_height = option_line_height,
		};
	}
}

void ui::render_dropdown(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	const auto& dropdown_data = std::get<DropdownElementData>(element.element->data);

	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;
	float expand_anim = element.animations.at(hasher("expand")).current;

	auto pos = get_positions(container, element, dropdown_data, expand_anim);

	// Background color
	int background_shade = 15 + (10 * hover_anim);
	int border_shade = 70;

	gfx::Color adjusted_color =
		utils::adjust_color(gfx::rgba(background_shade, background_shade, background_shade, 255), anim);
	gfx::Color text_color = utils::adjust_color(gfx::rgba(255, 255, 255, 255), anim);
	gfx::Color selected_text_color = utils::adjust_color(gfx::rgba(255, 100, 100, 255), anim);
	gfx::Color border_color = utils::adjust_color(gfx::rgba(border_shade, border_shade, border_shade, 255), anim);

	render::text(surface, pos.label_pos, text_color, dropdown_data.label, dropdown_data.font);

	// Render dropdown main area
	render::rounded_rect_filled(surface, pos.dropdown_rect, adjusted_color, DROPDOWN_ROUNDING);
	render::rounded_rect_stroke(surface, pos.dropdown_rect, border_color, DROPDOWN_ROUNDING);

	// Get currently selected option text
	render::text(surface, pos.selected_text_pos, text_color, *dropdown_data.selected, dropdown_data.font);

	// // Render dropdown arrow
	// gfx::Point arrow_pos(dropdown_rect.x2() - dropdown_arrow_size - 10, dropdown_rect.center().y);
	// std::vector<gfx::Point> arrow_points = {
	//     {arrow_pos.x, arrow_pos.y - dropdown_arrow_size/2},
	//     {arrow_pos.x + dropdown_arrow_size, arrow_pos.y},
	//     {arrow_pos.x, arrow_pos.y + dropdown_arrow_size/2}
	// };
	// render::polygon(surface, arrow_points, text_color);

	// Render dropdown options
	if (expand_anim > 0.01f) {
		gfx::Color option_color = utils::adjust_color(gfx::rgba(15, 15, 15, 255), anim);
		gfx::Color option_border_color =
			utils::adjust_color(gfx::rgba(border_shade, border_shade, border_shade, 255), anim);
		render::rounded_rect_filled(surface, pos.options_rect, option_color, DROPDOWN_ROUNDING);
		render::rounded_rect_stroke(surface, pos.options_rect, option_border_color, DROPDOWN_ROUNDING);

		render::push_clip_rect(surface, pos.options_rect);

		// Render options
		gfx::Point option_text_pos = pos.options_rect.origin();
		option_text_pos.y += OPTIONS_PADDING.h + pos.font_height + OPTION_LINE_HEIGHT_ADD / 2 - 1;
		option_text_pos.x = pos.options_rect.origin().x + OPTIONS_PADDING.w;

		for (const auto& option : dropdown_data.options) {
			bool selected = option == *dropdown_data.selected;

			render::text(
				surface, option_text_pos, selected ? selected_text_color : text_color, option, dropdown_data.font
			);

			option_text_pos.y += pos.option_line_height;
		}

		render::pop_clip_rect(surface);
	}

	// render::rect_stroke(surface, element.element->rect, gfx::rgba(255, 0, 0, 100));
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
bool ui::update_dropdown(const Container& container, AnimatedElement& element) {
	const auto& dropdown_data = std::get<DropdownElementData>(element.element->data);

	auto& hover_anim = element.animations.at(hasher("hover"));
	auto& expand_anim = element.animations.at(hasher("expand"));

	auto pos = get_positions(container, element, dropdown_data, expand_anim.current);

	bool hovered = pos.dropdown_rect.contains(keys::mouse_pos);
	hover_anim.set_goal(hovered ? 1.f : 0.f);

	bool active = active_element == &element;

	auto toggle_active = [&] {
		if (active) {
			active_element = nullptr;
			active = false;
		}
		else {
			active_element = &element;
			active = true;
		}

		expand_anim.set_goal(active ? 1.f : 0.f);
	};

	bool res = false;

	if (hovered && keys::is_mouse_down()) {
		// toggle dropdown
		toggle_active();
		keys::on_mouse_press_handled(os::Event::MouseButton::LeftButton);

		res = true;
	}
	else if (active) {
		// clicking options
		if (keys::is_mouse_down()) {
			if (pos.options_rect.contains(keys::mouse_pos)) {
				size_t clicked_option_index = (keys::mouse_pos.y - pos.options_rect.y) / pos.option_line_height;

				// eat all inputs cause otherwise itll click stuff behind
				keys::on_mouse_press_handled(os::Event::LeftButton);

				if (clicked_option_index >= 0 && clicked_option_index < dropdown_data.options.size()) {
					std::string new_selected = dropdown_data.options[clicked_option_index];
					if (new_selected != *dropdown_data.selected) {
						*dropdown_data.selected = new_selected;
						toggle_active();

						if (dropdown_data.on_change)
							(*dropdown_data.on_change)(dropdown_data.selected);

						res = true;
					}
				}
			}
			else {
				toggle_active();
			}
		}
	}

	// update z index
	int z_index = 0;
	if (active)
		z_index = 2;
	else if (expand_anim.current > 0.01f)
		z_index = 1;
	element.z_index = z_index;

	return res;
}

// NOLINTEND(readability-function-cognitive-complexity)

ui::Element& ui::add_dropdown(
	const std::string& id,
	Container& container,
	const std::string& label,
	const std::vector<std::string>& options,
	std::string& selected,
	const SkFont& font,
	std::optional<std::function<void(std::string*)>> on_change
) {
	gfx::Size max_text_size(0, font.getSize());

	// Find max text size for width calculation
	for (const auto& option : options) {
		gfx::Size text_size = render::get_text_size(option, font);
		max_text_size.w = std::max(max_text_size.w, text_size.w);
	}

	gfx::Size total_size(200, font.getSize() + LABEL_GAP + max_text_size.h + (DROPDOWN_PADDING.h * 2));

	Element element = {
		.type = ElementType::DROPDOWN,
		.rect = gfx::Rect(container.current_position, total_size),
		.data =
			DropdownElementData{
				.label = label,
				.options = options,
				.selected = &selected,
				.font = font,
				.on_change = std::move(on_change),
			},
		.render_fn = render_dropdown,
		.update_fn = update_dropdown,
	};

	return *add_element(
		container,
		id,
		std::move(element),
		container.line_height,
		{
			{ hasher("main"), { .speed = 25.f } },
			{ hasher("hover"), { .speed = 80.f } },
			{ hasher("expand"), { .speed = 30.f } },
		}
	);
}
