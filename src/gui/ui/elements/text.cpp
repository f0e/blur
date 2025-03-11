#include "../ui.h"
#include "../render.h"
#include "../utils.h"

namespace {
	int get_text_height(const ui::Container& container, const std::vector<std::string>& lines, const SkFont& font) {
		return lines.size() * (std::max((int)font.getSize(), container.line_height));
	}

	std::vector<std::string> wrap_lines(
		const ui::Container& container, const std::vector<std::string>& lines, const SkFont& font
	) {
		auto size = container.get_usable_rect().size();

		std::vector<std::string> wrapped_lines;

		for (const auto& line : lines) {
			auto wrapped = render::wrap_text(line, size, font);

			if (wrapped.size() > 1)
				wrapped_lines.insert(wrapped_lines.end(), wrapped.begin(), wrapped.end());
			else
				wrapped_lines.push_back(line);
		}

		return wrapped_lines;
	}
}

void ui::render_text(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	const auto& text_data = std::get<TextElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	gfx::Color adjusted_color = utils::adjust_color(text_data.color, anim);
	gfx::Color adjusted_shadow_color = utils::adjust_color(gfx::rgba(0, 0, 0), anim);

	gfx::Point text_pos = element.element->rect.origin();
	text_pos.y += text_data.font.getSize() - 1;

	for (const auto& line : text_data.lines) {
		if (text_data.style == TextStyle::OUTLINE) {
			render::text(
				surface, text_pos + gfx::Point{ 1, -1 }, adjusted_shadow_color, line, text_data.font, text_data.align
			);
			render::text(
				surface, text_pos + gfx::Point{ -1, -1 }, adjusted_shadow_color, line, text_data.font, text_data.align
			);
			render::text(
				surface, text_pos + gfx::Point{ -1, 1 }, adjusted_shadow_color, line, text_data.font, text_data.align
			);
		}

		if (text_data.style == TextStyle::DROPSHADOW || text_data.style == TextStyle::OUTLINE) {
			render::text(
				surface, text_pos + gfx::Point{ 1, 1 }, adjusted_shadow_color, line, text_data.font, text_data.align
			);
		}

		render::text(surface, text_pos, adjusted_color, line, text_data.font, text_data.align);
		text_pos.y += container.line_height;
	}
}

ui::Element& ui::add_text(
	const std::string& id,
	Container& container,
	const std::string& text,
	gfx::Color color,
	const SkFont& font,
	os::TextAlign align,
	TextStyle style
) {
	auto lines = render::wrap_text(text, container.get_usable_rect().size(), font);

	return add_text(id, container, lines, color, font, align, style);
}

ui::Element& ui::add_text(
	const std::string& id,
	Container& container,
	std::vector<std::string> lines,
	gfx::Color color,
	const SkFont& font,
	os::TextAlign align,
	TextStyle style
) {
	lines = wrap_lines(container, lines, font);

	int text_height = get_text_height(container, lines, font);

	Element element(
		id,
		ElementType::TEXT,
		gfx::Rect(container.current_position, gfx::Size(0, text_height)), // todo: set width
		TextElementData{ .lines = lines, .color = color, .font = font, .align = align, .style = style },
		render_text
	);

	return *add_element(container, std::move(element), container.line_height);
}

ui::Element& ui::add_text_fixed(
	const std::string& id,
	Container& container,
	const gfx::Point& position,
	const std::string& text,
	gfx::Color color,
	const SkFont& font,
	os::TextAlign align,
	TextStyle style
) {
	auto lines = render::wrap_text(text, container.get_usable_rect().size(), font);

	return add_text_fixed(id, container, position, lines, color, font, align, style);
}

ui::Element& ui::add_text_fixed(
	const std::string& id,
	Container& container,
	const gfx::Point& position,
	std::vector<std::string> lines,
	gfx::Color color,
	const SkFont& font,
	os::TextAlign align,
	TextStyle style
) {
	lines = wrap_lines(container, lines, font);

	int text_height = get_text_height(container, lines, font);

	Element element(
		id,
		ElementType::TEXT,
		gfx::Rect(position, gfx::Size(0, text_height)), // todo: set width
		TextElementData{
			.lines = lines,
			.color = color,
			.font = font,
			.align = align,
			.style = style,
		},
		render_text,
		{},
		true
	);

	return *add_element(container, std::move(element), container.line_height);
}
