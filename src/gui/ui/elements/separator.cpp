#include <utility>

#include "../ui.h"
#include "../render.h"
#include "../utils.h"

#include "../../renderer.h"

void ui::render_separator(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	float anim = element.animations.at(hasher("main")).current;

	gfx::Color color = utils::adjust_color(gfx::rgba(255, 255, 255, 50), anim);

	gfx::Rect separator_rect = element.element->rect;
	separator_rect.y = separator_rect.center().y;
	separator_rect.h = 1;
	render::rect_filled_gradient(
		surface,
		separator_rect,
		render::GradientDirection::GRADIENT_RIGHT,
		{ color, utils::adjust_color(gfx::rgba(255, 255, 255, 50), 0.f) }
	);
}

ui::Element& ui::add_separator(const std::string& id, Container& container) {
	Element element = {
		.type = ElementType::SEPARATOR,
		.rect = gfx::Rect(container.current_position, gfx::Size(200, container.line_height)),
		.data = ElementData{},
		.render_fn = render_separator,
	};

	return *add_element(container, id, std::move(element), container.line_height);
}
