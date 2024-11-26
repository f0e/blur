#include "../ui.h"
#include "../render.h"
#include "../utils.h"

void ui::render_text(os::Surface* surface, const Element* element, float anim) {
	const auto& text_data = std::get<TextElementData>(element->data);

	gfx::Color adjusted_color = utils::adjust_color(text_data.color, anim);

	gfx::Point text_pos = element->rect.origin();
	text_pos.y += text_data.font.getSize() - 1;

	render::text(surface, text_pos, adjusted_color, text_data.text, text_data.font, text_data.align);
}

ui::Element& ui::add_text(
	const std::string& id,
	Container& container,
	const std::string& text,
	gfx::Color color,
	const SkFont& font,
	os::TextAlign align,
	int margin_bottom
) {
	Element element = {
		.type = ElementType::TEXT,
		.rect = gfx::Rect(container.current_position, gfx::Size(0, font.getSize())),
		.data =
			TextElementData{
				.text = text,
				.color = color,
				.font = font,
				.align = align,
			},
		.render_fn = render_text,
	};

	return *add_element(container, id, std::move(element), margin_bottom);
}

ui::Element& ui::add_text_fixed(
	const std::string& id,
	Container& container,
	const gfx::Point& position,
	const std::string& text,
	gfx::Color color,
	const SkFont& font,
	os::TextAlign align
) {
	Element element = {
		.type = ElementType::TEXT,
		.rect = gfx::Rect(position, gfx::Size(0, font.getSize())),
		.data =
			TextElementData{
				.text = text,
				.color = color,
				.font = font,
				.align = align,
			},
		.render_fn = render_text,
		.fixed = true,
	};

	return *add_element(container, id, std::move(element));
}
