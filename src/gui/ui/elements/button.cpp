#include "../ui.h"
#include "../../render/render.h"

#include "../keys.h"

const gfx::Size BUTTON_PADDING = { 11, 8 };
constexpr float BUTTON_ROUNDING = 7.f;
constexpr int BUTTON_ICON_GAP = 5;

constexpr int BUTTON_STROKE_SHADE = 80;
constexpr int BUTTON_SHADE = 17;
constexpr int BUTTON_HOVER_SHADE = 42;
constexpr int BUTTON_TEXT_SHADE = 255;
constexpr int BUTTON_TEXT_HOVER_SHADE = 255;

// how bright the accent fill gets on hover - full strength is far too loud behind white text
constexpr float BUTTON_ACCENT_FILL_MULT = 0.45f;

void ui::render_button(const Container& container, const AnimatedElement& element) {
	const auto& button_data = std::get<ButtonElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;

	int shade = u::lerp((float)BUTTON_SHADE, (float)BUTTON_HOVER_SHADE, hover_anim);
	int text_shade = u::lerp((float)BUTTON_TEXT_SHADE, (float)BUTTON_TEXT_HOVER_SHADE, hover_anim);

	gfx::Color fill_color(shade, shade, shade, anim * 255);
	gfx::Color stroke_color(BUTTON_STROKE_SHADE, BUTTON_STROKE_SHADE, BUTTON_STROKE_SHADE, anim * 255);
	gfx::Color text_color(text_shade, text_shade, text_shade, anim * 255);

	if (button_data.accent_color) {
		const auto& accent = *button_data.accent_color;

		fill_color = gfx::Color(
			gfx::Color::lerp(gfx::Color::gray(BUTTON_SHADE), accent * BUTTON_ACCENT_FILL_MULT, hover_anim), anim * 255
		);

		stroke_color = gfx::Color(accent, anim * 255);
		text_color = gfx::Color(gfx::Color::lerp(accent, gfx::Color::white(), hover_anim), anim * 255);
	}

	// fill
	render::rounded_rect_filled(element.element->rect, fill_color, BUTTON_ROUNDING);

	// border
	render::rounded_rect_stroke(element.element->rect, stroke_color, BUTTON_ROUNDING);

	const gfx::Size text_size = button_data.font.calc_size(button_data.text);
	const gfx::Size icon_size = button_data.icon ? fonts::icons.calc_size(*button_data.icon) : gfx::Size{};
	const int icon_and_gap_width =
		button_data.icon ? icon_size.w + (button_data.text.empty() ? 0 : BUTTON_ICON_GAP) : 0;
	const int content_width = icon_and_gap_width + text_size.w;
	const int content_x = element.element->rect.center().x - content_width / 2;

	if (button_data.icon) {
		render::text(
			gfx::Point(content_x + icon_size.w / 2, element.element->rect.center().y),
			text_color,
			*button_data.icon,
			fonts::icons,
			FONT_CENTERED_X | FONT_CENTERED_Y
		);
	}

	if (!button_data.text.empty()) {
		render::text(
			gfx::Point(content_x + icon_and_gap_width, element.element->rect.center().y),
			text_color,
			button_data.text,
			button_data.font,
			FONT_CENTERED_Y
		);
	}
}

bool ui::update_button(const Container& container, AnimatedElement& element) {
	const auto& button_data = std::get<ButtonElementData>(element.element->data);

	auto& anim = element.animations.at(hasher("hover"));

	bool hovered = element.element->rect.contains(keys::mouse_pos) && set_hovered_element(element);
	anim.set_goal(hovered ? 1.f : 0.f);

	if (hovered) {
		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

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

int ui::button_height(const render::Font& font) {
	return font.height() + (BUTTON_PADDING.h * 2);
}

ui::AnimatedElement* ui::add_button(
	const std::string& id,
	Container& container,
	const std::string& text,
	const render::Font& font,
	std::optional<std::function<void()>> on_press,
	std::optional<gfx::Color> accent_color,
	std::optional<std::string> icon
) {
	const gfx::Size text_size = font.calc_size(text);
	const gfx::Size icon_size = icon ? fonts::icons.calc_size(*icon) : gfx::Size{};
	const int icon_and_gap_width = icon ? icon_size.w + (text.empty() ? 0 : BUTTON_ICON_GAP) : 0;
	const int height = button_height(font);
	const int button_width = text.empty() && icon ? height : text_size.w + icon_and_gap_width + (BUTTON_PADDING.w * 2);

	gfx::Rect rect(container.current_position, gfx::Size(button_width, height));

	Element element(
		id,
		ElementType::BUTTON,
		rect,
		ButtonElementData{
			.text = text,
			.font = font,
			.on_press = std::move(on_press),
			.accent_color = accent_color,
			.icon = std::move(icon),
		},
		render_button,
		update_button
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
