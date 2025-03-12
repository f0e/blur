#include <utility>

#include "../ui.h"
#include "../render.h"
#include "../utils.h"

#include "../../renderer.h"

void ui::render_separator(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	const auto& separator_data = std::get<SeparatorElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	gfx::Color color = utils::adjust_color(gfx::rgba(255, 255, 255, 50), anim);

	gfx::Rect separator_rect = element.element->rect;
	separator_rect.y = separator_rect.center().y;
	separator_rect.h = 1;

	switch (separator_data.style) {
		case SeparatorStyle::NORMAL: {
			render::rect_filled(surface, separator_rect, color);
			break;
		}
		case SeparatorStyle::FADE_RIGHT: {
			render::rect_filled_gradient(
				surface,
				separator_rect,
				render::GradientDirection::GRADIENT_RIGHT,
				{ color, utils::adjust_color(gfx::rgba(255, 255, 255, 50), 0.f) }
			);
			break;
		}
		case SeparatorStyle::FADE_LEFT: {
			render::rect_filled_gradient(
				surface,
				separator_rect,
				render::GradientDirection::GRADIENT_LEFT,
				{ color, utils::adjust_color(gfx::rgba(255, 255, 255, 50), 0.f) }
			);
			break;
		}
		case SeparatorStyle::FADE_BOTH: {
			auto left_rect = separator_rect;
			left_rect.w /= 2;

			auto right_rect = left_rect;
			right_rect.x += right_rect.w;

			render::rect_filled_gradient(
				surface,
				left_rect,
				render::GradientDirection::GRADIENT_LEFT,
				{ color, utils::adjust_color(gfx::rgba(255, 255, 255, 50), 0.f) }
			);
			render::rect_filled_gradient(
				surface,
				right_rect,
				render::GradientDirection::GRADIENT_RIGHT,
				{ color, utils::adjust_color(gfx::rgba(255, 255, 255, 50), 0.f) }
			);
			break;
		}
	}
}

ui::Element& ui::add_separator(const std::string& id, Container& container, SeparatorStyle style) {
	Element element(
		id,
		ElementType::SEPARATOR,
		gfx::Rect(container.current_position, gfx::Size(200, container.line_height)),
		SeparatorElementData{
			.style = style,
		},
		render_separator
	);

	return *add_element(container, std::move(element), container.line_height);
}
