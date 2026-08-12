#include "../ui.h"
#include "../../render/render.h"

namespace {
	constexpr float SPIN_DEGREES_PER_SECOND = 320.f;
	constexpr float SPIN_ARC_DEGREES = 270.f;
	constexpr int SPIN_SEGMENTS = 32;
}

void ui::render_spinner(const Container& container, const AnimatedElement& element) {
	const auto& spinner_data = std::get<SpinnerElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	gfx::Color color = spinner_data.color.adjust_alpha(anim);

	float rotation = std::fmod((float)ImGui::GetTime() * SPIN_DEGREES_PER_SECOND, 360.f);

	render::circle_stroke(
		element.element->rect.center(),
		spinner_data.radius,
		color,
		spinner_data.thickness,
		SPIN_SEGMENTS,
		rotation + SPIN_ARC_DEGREES,
		rotation
	);
}

ui::AnimatedElement* ui::add_spinner(
	const std::string& id, Container& container, float radius, gfx::Color color, float thickness
) {
	int size = (int)((radius + thickness) * 2.f);

	Element element(
		id,
		ElementType::SPINNER,
		gfx::Rect(container.current_position, gfx::Size(container.get_usable_rect().w, size)),
		SpinnerElementData{
			.color = color,
			.radius = radius,
			.thickness = thickness,
		},
		render_spinner
	);

	return add_element(container, std::move(element), container.element_gap);
}
