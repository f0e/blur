#include <utility>

#include "../ui.h"
#include "../render.h"
#include "../utils.h"

#include "../../renderer.h"

void ui::render_separator(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	float anim = element.animations.at(hasher("main")).current;

	gfx::Color color = utils::adjust_color(gfx::rgba(255, 255, 255, 80), anim);

	render::line(
		surface,
		element.element->rect.origin(),
		gfx::Point(element.element->rect.x2(), element.element->rect.y),
		color,
		0.5f
	);
}

ui::Element& ui::add_separator(const std::string& id, Container& container) {
	Element element = {
		.type = ElementType::SEPARATOR,
		.rect = gfx::Rect(container.current_position, gfx::Size(200, 1)),
		.data = ElementData{},
		.render_fn = render_separator,
	};

	return *add_element(container, id, std::move(element), container.line_height);
}
