#include "../ui.h"
#include "../../render/render.h"

void ui::render_spinner(const Container& container, const AnimatedElement& element) {
	const auto& spinner_data = std::get<SpinnerElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	render::spinner(
		element.element->rect.center(),
		spinner_data.radius,
		spinner_data.background_color,
		spinner_data.highlight_color,
		spinner_data.thickness,
		anim,
		spinner_data.trail_degrees
	);
}

ui::AnimatedElement* ui::add_spinner(
	const std::string& id,
	Container& container,
	float radius,
	gfx::Color background_color,
	gfx::Color highlight_color,
	float thickness,
	float trail_degrees
) {
	int size = (int)((radius + thickness) * 2.f);

	Element element(
		id,
		ElementType::SPINNER,
		gfx::Rect(container.current_position, gfx::Size(container.get_usable_rect().w, size)),
		SpinnerElementData{
			.background_color = background_color,
			.highlight_color = highlight_color,
			.radius = radius,
			.thickness = thickness,
			.trail_degrees = trail_degrees,
		},
		render_spinner,
		{},
		{},
		{},
		false,
		true
	);

	return add_element(container, std::move(element), container.element_gap);
}
