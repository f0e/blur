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

struct OptionsRect {
	gfx::Rect rect;
	float line_height;
};

namespace {
	OptionsRect get_options_rect(
		const ui::Container& container, const ui::AnimatedElement& element, const ui::DropdownElementData& dropdown_data
	) {
		float option_line_height = dropdown_data.font.getSize() + OPTION_LINE_HEIGHT_ADD;

		gfx::Rect options_rect = element.element->rect;
		options_rect.y = options_rect.y2() + OPTIONS_GAP;
		options_rect.h = option_line_height * dropdown_data.options.size() + OPTIONS_PADDING.h * 2;

		// if it'll pass the end of the container open upwards
		if (options_rect.y2() > container.rect.y2())
			options_rect.y -= options_rect.h + option_line_height + OPTIONS_GAP;

		return {
			.rect = options_rect,
			.line_height = option_line_height,
		};
	}
}

void ui::render_dropdown(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	const auto& dropdown_data = std::get<DropdownElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;
	float expand_anim = element.animations.at(hasher("expand")).current;

	// Background color
	int background_shade = 20 + (20 * hover_anim);
	gfx::Color adjusted_color =
		utils::adjust_color(gfx::rgba(background_shade, background_shade, background_shade, 255), anim);
	gfx::Color text_color = utils::adjust_color(gfx::rgba(255, 255, 255, 255), anim);
	gfx::Color selected_text_color = utils::adjust_color(gfx::rgba(255, 100, 100, 255), anim);
	gfx::Color border_color = utils::adjust_color(gfx::rgba(100, 100, 100, 255), anim);

	gfx::Point label_pos = element.element->rect.origin();
	label_pos.y += dropdown_data.font.getSize();

	render::text(surface, label_pos, text_color, dropdown_data.label, dropdown_data.font);

	gfx::Rect dropdown_rect = element.element->rect;
	dropdown_rect.y = label_pos.y + LABEL_GAP;
	dropdown_rect.h -= dropdown_rect.y - element.element->rect.y;

	// Render dropdown main area
	render::rounded_rect_filled(surface, dropdown_rect, adjusted_color, DROPDOWN_ROUNDING);
	render::rounded_rect_stroke(surface, dropdown_rect, border_color, DROPDOWN_ROUNDING);

	// Render selected option text
	gfx::Point selected_text_pos = dropdown_rect.origin();
	selected_text_pos.x += DROPDOWN_PADDING.w;
	selected_text_pos.y = dropdown_rect.center().y + dropdown_data.font.getSize() / 2 - 1;

	// Get currently selected option text
	render::text(surface, selected_text_pos, text_color, *dropdown_data.selected, dropdown_data.font);

	// // Render dropdown arrow
	// gfx::Point arrow_pos(dropdown_rect.x2() - dropdown_arrow_size - 10, dropdown_rect.center().y);
	// std::vector<gfx::Point> arrow_points = {
	//     {arrow_pos.x, arrow_pos.y - dropdown_arrow_size/2},
	//     {arrow_pos.x + dropdown_arrow_size, arrow_pos.y},
	//     {arrow_pos.x, arrow_pos.y + dropdown_arrow_size/2}
	// };
	// render::polygon(surface, arrow_points, text_color);

	// Render dropdown options if expanded
	if (active_element == &element) {
		auto options_rect = get_options_rect(container, element, dropdown_data);

		gfx::Color option_color = utils::adjust_color(gfx::rgba(20, 20, 20, 255), anim);
		gfx::Color option_border_color = utils::adjust_color(gfx::rgba(100, 100, 100, 255), anim);
		render::rounded_rect_filled(surface, options_rect.rect, option_color, DROPDOWN_ROUNDING);
		render::rounded_rect_stroke(surface, options_rect.rect, option_border_color, DROPDOWN_ROUNDING);

		render::push_clip_rect(surface, surface->bounds()); // options_rect.rect);

		// Render options
		gfx::Point option_text_pos = options_rect.rect.origin();
		option_text_pos.y += OPTIONS_PADDING.h + dropdown_data.font.getSize() + OPTION_LINE_HEIGHT_ADD / 2 - 1;
		option_text_pos.x = options_rect.rect.origin().x + OPTIONS_PADDING.w;

		for (const auto& option : dropdown_data.options) {
			bool selected = option == *dropdown_data.selected;

			render::text(
				surface, option_text_pos, selected ? selected_text_color : text_color, option, dropdown_data.font
			);

			option_text_pos.y += options_rect.line_height;
		}

		render::pop_clip_rect(surface);
	}
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
bool ui::update_dropdown(const Container& container, AnimatedElement& element) {
	const auto& dropdown_data = std::get<DropdownElementData>(element.element->data);

	auto& hover_anim = element.animations.at(hasher("hover"));
	auto& expand_anim = element.animations.at(hasher("expand"));

	// todo: stop redefining rects like this in all elements
	gfx::Point label_pos = element.element->rect.origin();
	label_pos.y += dropdown_data.font.getSize();

	gfx::Rect dropdown_rect = element.element->rect;
	dropdown_rect.y = label_pos.y + LABEL_GAP;
	dropdown_rect.h -= dropdown_rect.y - element.element->rect.y;

	bool hovered = dropdown_rect.contains(keys::mouse_pos);
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

	if (hovered && keys::is_mouse_down()) {
		// toggle dropdown
		toggle_active();
		keys::on_mouse_press_handled(os::Event::MouseButton::LeftButton);

		return true;
	}

	if (active) {
		auto options_rect = get_options_rect(container, element, dropdown_data);

		if (keys::is_mouse_down()) {
			if (options_rect.rect.contains(keys::mouse_pos)) {
				size_t clicked_option_index = (keys::mouse_pos.y - options_rect.rect.y) / options_rect.line_height;

				// eat all inputs cause otherwise itll click stuff behind
				keys::on_mouse_press_handled(os::Event::LeftButton);

				if (clicked_option_index >= 0 && clicked_option_index < dropdown_data.options.size()) {
					std::string new_selected = dropdown_data.options[clicked_option_index];
					if (new_selected != *dropdown_data.selected) {
						*dropdown_data.selected = new_selected;
						toggle_active();

						if (dropdown_data.on_change)
							(*dropdown_data.on_change)(dropdown_data.selected);

						return true;
					}
				}
			}
			else {
				toggle_active();
			}
		}
	}

	return false;
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
			{ hasher("hover"), { .speed = 40.f } },
			{ hasher("expand"), { .speed = 40.f } },
		}
	);
}
