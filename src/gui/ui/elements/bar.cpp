#include "../ui.h"
#include "../../render/render.h"

void ui::render_bar(const Container& container, const AnimatedElement& element) {
	const int text_gap = 7;

	const auto& bar_data = std::get<BarElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	gfx::Color adjusted_background_color = bar_data.background_color.adjust_alpha(anim);
	gfx::Color adjusted_fill_color = bar_data.fill_color.adjust_alpha(anim);

	gfx::Size text_size(0, 0);

	if (bar_data.bar_text && bar_data.text_color && bar_data.font) {
		gfx::Color adjusted_text_color = (*bar_data.text_color).adjust_alpha(anim);

		gfx::Point text_pos = element.element->rect.origin();
		text_pos.y += element.element->rect.h / 2 - 1; // -1 for optical alignment or whatever

		text_size = bar_data.font->calc_size(*bar_data.bar_text);

		text_pos.x = element.element->rect.x2();

		render::text(
			text_pos, adjusted_text_color, *bar_data.bar_text, *bar_data.font, FONT_RIGHT_ALIGN | FONT_CENTERED_Y
		);
	}

	gfx::Rect bar_rect = element.element->rect;
	bar_rect.w -= text_size.w + text_gap;

	render::rounded_rect_filled(bar_rect, adjusted_background_color, 4.f);

	if (bar_data.percent_fill > 0) {
		gfx::Rect fill_rect = bar_rect;
		fill_rect.w = static_cast<int>(bar_rect.w * bar_data.percent_fill);
		render::rounded_rect_filled(fill_rect, adjusted_fill_color, 4.f);
	}
}

ui::AnimatedElement* ui::add_bar(
	const std::string& id,
	Container& container,
	float percent_fill,
	gfx::Color background_color,
	gfx::Color fill_color,
	int bar_width,
	std::optional<std::string> bar_text,
	std::optional<gfx::Color> text_color,
	std::optional<render::Font> font
) {
	Element element(
		id,
		ElementType::BAR,
		gfx::Rect(container.current_position, gfx::Size(bar_width, 6)),
		BarElementData{
			.percent_fill = percent_fill,
			.background_color = background_color,
			.fill_color = fill_color,
			.bar_text = std::move(bar_text),
			.text_color = text_color,
			.font = font,
		},
		render_bar
	);

	return add_element(container, std::move(element), container.element_gap);
}
