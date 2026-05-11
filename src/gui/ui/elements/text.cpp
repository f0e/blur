#include "../ui.h"
#include "../../render/render.h"

namespace {
	int get_line_spacing(const ui::Container& container, const render::Font& font) {
		return font.height() * container.line_height;
	}

	int get_text_height(
		const ui::Container& container, const std::vector<std::string>& lines, const render::Font& font
	) {
		return lines.size() * get_line_spacing(container, font);
	}

	std::vector<std::string> wrap_lines(
		const ui::Container& container, const std::vector<std::string>& lines, const render::Font& font
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

void ui::render_text(const Container& container, const AnimatedElement& element) {
	const auto& text_data = std::get<TextElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	gfx::Color adjusted_color = text_data.color.adjust_alpha(anim);
	gfx::Color adjusted_shadow_color = gfx::Color(0, 0, 0, anim * 255);

	gfx::Point text_pos = element.element->rect.origin();

	if (text_data.flags & FONT_CENTERED_X)
		text_pos.x += element.element->rect.w / 2;

	if (text_data.flags & FONT_RIGHT_ALIGN)
		text_pos.x = element.element->rect.x2();

	for (const auto& line : text_data.lines) {
		render::text(text_pos, adjusted_color, line, *text_data.font, text_data.flags);
		text_pos.y += get_line_spacing(container, *text_data.font);
	}
}

ui::AnimatedElement* ui::add_text(
	const std::string& id,
	Container& container,
	const std::string& text,
	gfx::Color color,
	const render::Font& font,
	unsigned int flags
) {
	auto lines = render::wrap_text(text, container.get_usable_rect().size(), font);

	return add_text(id, container, lines, color, font, flags);
}

ui::AnimatedElement* ui::add_text(
	const std::string& id,
	Container& container,
	std::vector<std::string> lines,
	gfx::Color color,
	const render::Font& font,
	unsigned int flags
) {
	lines = wrap_lines(container, lines, font);

	int text_height = get_text_height(container, lines, font);

	Element element(
		id,
		ElementType::TEXT,
		gfx::Rect(container.current_position, gfx::Size(container.get_usable_rect().w, text_height)),
		TextElementData{
			.lines = lines,
			.color = color,
			.font = &font,
			.flags = flags,
		},
		render_text
	);

	return add_element(container, std::move(element), container.element_gap);
}

ui::AnimatedElement* ui::add_text_fixed(
	const std::string& id,
	Container& container,
	const gfx::Point& position,
	const std::string& text,
	gfx::Color color,
	const render::Font& font,
	unsigned int flags
) {
	auto lines = render::wrap_text(text, container.get_usable_rect().size(), font);

	return add_text_fixed(id, container, position, lines, color, font, flags);
}

ui::AnimatedElement* ui::add_text_fixed(
	const std::string& id,
	Container& container,
	const gfx::Point& position,
	std::vector<std::string> lines,
	gfx::Color color,
	const render::Font& font,
	unsigned int flags
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
			.font = &font,
			.flags = flags,
		},
		render_text,
		{},
		{},
		true
	);

	return add_element(container, std::move(element), container.element_gap);
}
