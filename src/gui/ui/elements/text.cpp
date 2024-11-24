#include "../ui.h"
#include "../render.h"
#include "../utils.h"

void ui::render_text(os::Surface* surface, const Element* element, float anim) {
	auto& text_data = std::get<TextElementData>(element->data);

	gfx::Color adjusted_color = utils::adjust_color(text_data.color, anim);

	gfx::Point text_pos = element->rect.origin();
	text_pos.y += text_data.font.getSize() - 1;

	render::text(surface, text_pos, adjusted_color, text_data.text, text_data.font, text_data.align);
}

std::shared_ptr<ui::Element> ui::add_text(const std::string& id, Container& container, const std::string& text, gfx::Color color, const SkFont& font, os::TextAlign align, int margin_bottom) {
	auto element = std::make_shared<Element>(Element{
		ElementType::TEXT,
		gfx::Rect(container.current_position, gfx::Size(0, font.getSize())),
		TextElementData{ text, color, font, align },
		render_text,
	});

	add_element(container, id, element, margin_bottom);

	return element;
}

std::shared_ptr<ui::Element> ui::add_text_fixed(const std::string& id, Container& container, gfx::Point position, const std::string& text, gfx::Color color, const SkFont& font, os::TextAlign align, int margin_bottom) {
	auto element = std::make_shared<Element>(Element{
		ElementType::TEXT,
		gfx::Rect(position, gfx::Size(0, font.getSize())),
		TextElementData{ text, color, font, align },
		render_text,
		true,
	});

	add_element_fixed(container, id, element);

	return element;
}
