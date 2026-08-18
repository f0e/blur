#include "../ui.h"
#include "../../render/render.h"

#include "../keys.h"

void ui::render_icon_button(const Container& container, const AnimatedElement& element) {
	const auto& button_data = std::get<IconButtonElementData>(element.element->data);

	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;

	gfx::Color color = gfx::Color::lerp(button_data.color, button_data.hover_color, hover_anim).adjust_alpha(anim);

	render::text(
		element.element->rect.center(), color, button_data.icon, button_data.font, FONT_CENTERED_X | FONT_CENTERED_Y
	);
}

bool ui::update_icon_button(const Container& container, AnimatedElement& element) {
	const auto& button_data = std::get<IconButtonElementData>(element.element->data);

	auto& anim = element.animations.at(hasher("hover"));

	bool hovered = element.element->rect.contains(keys::mouse_pos) && set_hovered_element(element);
	anim.set_goal(hovered ? 1.f : 0.f);

	if (hovered) {
		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

		if (!button_data.tooltip.empty())
			tooltip::set(button_data.tooltip);

		if (button_data.on_press) {
			if (keys::is_mouse_down()) {
				(*button_data.on_press)();
				keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

				return true;
			}
		}
	}

	return false;
}

ui::AnimatedElement* ui::add_icon_button(
	const std::string& id,
	Container& container,
	const std::string& icon,
	const render::Font& font,
	const gfx::Size& size,
	gfx::Color color,
	gfx::Color hover_color,
	std::optional<std::function<void()>> on_press,
	const std::string& tooltip
) {
	Element element(
		id,
		ElementType::ICON_BUTTON,
		gfx::Rect(container.current_position, size),
		IconButtonElementData{
			.icon = icon,
			.font = font,
			.color = color,
			.hover_color = hover_color,
			.tooltip = tooltip,
			.on_press = std::move(on_press),
		},
		render_icon_button,
		update_icon_button
	);

	return add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(25.f) },
			{ hasher("hover"), AnimationState(80.f) },
		}
	);
}
