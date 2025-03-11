#include <utility>

#include "../ui.h"
#include "../render.h"
#include "../utils.h"
#include "../keys.h"

#include "../../renderer.h"

void ui::render_button(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	const float button_rounding = 7.8f;

	const auto& button_data = std::get<ButtonElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;

	int shade = 20 + (20 * hover_anim);
	gfx::Color adjusted_color = utils::adjust_color(gfx::rgba(shade, shade, shade, 255), anim);
	gfx::Color adjusted_text_color = utils::adjust_color(gfx::rgba(255, 255, 255, 255), anim);

	gfx::Point text_pos = element.element->rect.center();

	text_pos.y += button_data.font.getSize() / 2 - 1;

	// fill
	render::rounded_rect_filled(surface, element.element->rect, adjusted_color, button_rounding);

	// border
	render::rounded_rect_stroke(
		surface, element.element->rect, utils::adjust_color(gfx::rgba(100, 100, 100, 255), anim), button_rounding
	);

	render::text(surface, text_pos, adjusted_text_color, button_data.text, button_data.font, os::TextAlign::Center);
}

bool ui::update_button(const Container& container, AnimatedElement& element) {
	const auto& button_data = std::get<ButtonElementData>(element.element->data);

	auto& anim = element.animations.at(hasher("hover"));

	bool hovered = element.element->rect.contains(keys::mouse_pos) && set_hovered_element(element);
	anim.set_goal(hovered ? 1.f : 0.f);

	if (hovered) {
		set_cursor(os::NativeCursor::Link);

		if (button_data.on_press) {
			if (keys::is_mouse_down()) {
				(*button_data.on_press)();
				keys::on_mouse_press_handled(os::Event::MouseButton::LeftButton);

				return true;
			}
		}
	}

	return false;
}

ui::Element& ui::add_button(
	const std::string& id,
	Container& container,
	const std::string& text,
	const SkFont& font,
	std::optional<std::function<void()>> on_press
) {
	const gfx::Size button_padding(40, 20);

	gfx::Size text_size = render::get_text_size(text, font);

	Element element(
		id,
		ElementType::BUTTON,
		gfx::Rect(container.current_position, text_size + button_padding),
		ButtonElementData{
			.text = text,
			.font = font,
			.on_press = std::move(on_press),
		},
		render_button,
		update_button
	);

	return *add_element(
		container,
		std::move(element),
		container.line_height,
		{
			{ hasher("main"), { .speed = 25.f } },
			{ hasher("hover"), { .speed = 80.f } },
		}
	);
}
