#include <utility>

#include "../ui.h"
#include "../render.h"
#include "../utils.h"
#include "../keys.h"

#include "../../renderer.h"

void ui::render_button(os::Surface* surface, const Element* element, float anim) {
	const float button_rounding = 7.8f;

	const auto& button_data = std::get<ButtonElementData>(element->data);

	bool hovered = element->rect.contains(keys::mouse_pos);
	if (hovered) {
		gui::renderer::set_cursor(os::NativeCursor::Link);

		if (button_data.on_press) {
			if (keys::is_mouse_down()) {
				(*button_data.on_press)();
				keys::on_mouse_press_handled(os::Event::MouseButton::LeftButton);
			}
		}
	}

	int shade = hovered ? 40 : 20; // todo: lerp hover shade
	gfx::Color adjusted_color = utils::adjust_color(gfx::rgba(shade, shade, shade, 255), anim);
	gfx::Color adjusted_text_color = utils::adjust_color(gfx::rgba(255, 255, 255, 255), anim);

	gfx::Point text_pos = element->rect.center();

	text_pos.y += button_data.font.getSize() / 2 - 1;

	// fill
	render::rounded_rect_filled(surface, element->rect, adjusted_color, button_rounding);

	// border
	render::rounded_rect_stroke(
		surface, element->rect, utils::adjust_color(gfx::rgba(100, 100, 100, 255), anim), button_rounding
	);

	render::text(surface, text_pos, adjusted_text_color, button_data.text, button_data.font, os::TextAlign::Center);
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

	Element element = {
		.type = ElementType::BUTTON,
		.rect = gfx::Rect(container.current_position, text_size + button_padding),
		.data =
			ButtonElementData{
				.text = text,
				.font = font,
				.on_press = std::move(on_press),
			},
		.render_fn = render_button,
	};

	return *add_element(container, id, std::move(element), container.line_height);
}
