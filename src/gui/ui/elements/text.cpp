#include "../ui.h"
#include "../render.h"
#include "../utils.h"

void ui::render_text(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	const auto& text_data = std::get<TextElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	gfx::Color adjusted_color = utils::adjust_color(text_data.color, anim);

	gfx::Point text_pos = element.element->rect.origin();
	text_pos.y += text_data.font.getSize() - 1;

	for (const auto& line : text_data.lines) {
		render::text(surface, text_pos, adjusted_color, line, text_data.font, text_data.align);
		text_pos.y += text_data.font.getSize();
	}
}

// just so i dont have to copy this stuff twice
namespace {
	struct WrappedText {
		std::vector<std::string> lines;
		int text_height;
	};

	WrappedText wrap_text(ui::Container& container, const std::string& text, const SkFont& font) {
		int text_height = font.getSize();
		std::vector<std::string> lines = render::wrap_text(text, container.rect.size(), font);
		text_height *= lines.size(); // todo: line spacing

		return {
			.lines = lines,
			.text_height = text_height,
		};
	}
}

ui::Element& ui::add_text(
	const std::string& id,
	Container& container,
	const std::string& text,
	gfx::Color color,
	const SkFont& font,
	os::TextAlign align
) {
	auto wrapped_text = wrap_text(container, text, font);

	Element element = {
		.type = ElementType::TEXT,
		.rect = gfx::Rect(container.current_position, gfx::Size(0, wrapped_text.text_height)), // todo: set width
		.data =
			TextElementData{
				.lines = wrapped_text.lines,
				.color = color,
				.font = font,
				.align = align,
			},
		.render_fn = render_text,
	};

	return *add_element(container, id, std::move(element), container.line_height);
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
	auto wrapped_text = wrap_text(container, text, font);

	Element element = {
		.type = ElementType::TEXT,
		.rect = gfx::Rect(position, gfx::Size(0, wrapped_text.text_height)),
		.data =
			TextElementData{
				.lines = wrapped_text.lines,
				.color = color,
				.font = font,
				.align = align,
			},
		.render_fn = render_text,
		.fixed = true,
	};

	return *add_element(container, id, std::move(element));
}
