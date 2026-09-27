#include "../ui.h"
#include "../../render/render.h"

constexpr float HINT_ROUNDING = 4.f;
constexpr gfx::Size HINT_PADDING = { 8, 8 };
constexpr int PARAGRAPH_SPACING = 8;

namespace {
	int get_line_height(const ui::Container& container, const render::Font& font) {
		return font.height() * container.line_height;
	}

	std::vector<ui::Paragraph> wrap_paragraphs(
		const ui::Container& container, const std::vector<std::string>& input_lines, const render::Font& font
	) {
		auto size = container.get_usable_rect().size();
		std::vector<ui::Paragraph> paragraphs;

		for (size_t i = 0; i < input_lines.size(); ++i) {
			auto wrapped = render::wrap_text(input_lines[i], size, font);

			ui::Paragraph paragraph;
			paragraph.lines = wrapped;
			paragraph.is_last = (i == input_lines.size() - 1);

			paragraphs.push_back(paragraph);
		}

		return paragraphs;
	}

	int calculate_total_height(
		const ui::Container& container, const std::vector<ui::Paragraph>& paragraphs, const render::Font& font
	) {
		int height = 0;
		int line_height = get_line_height(container, font);

		for (const auto& paragraph : paragraphs) {
			// Add height for all lines in this paragraph
			height += paragraph.lines.size() * line_height;

			// Add paragraph spacing after each paragraph except the last
			if (!paragraph.is_last) {
				height += PARAGRAPH_SPACING;
			}
		}

		return height;
	}
}

void ui::render_hint(const Container& container, const AnimatedElement& element) {
	const auto& text_data = std::get<HintElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	gfx::Color adjusted_colour = text_data.color.adjust_alpha(anim);
	gfx::Color adjusted_shadow_colour = gfx::Color(0, 0, 0, anim * 255);
	gfx::Color border_colour = gfx::Color(255, 255, 255, anim * 50);

	render::rounded_rect_filled(element.element->rect, adjusted_shadow_colour, HINT_ROUNDING);
	render::rounded_rect_stroke(element.element->rect, border_colour, HINT_ROUNDING);

	gfx::Point text_pos = { element.element->rect.center().x, element.element->rect.origin().y + HINT_PADDING.h };
	int line_height = get_line_height(container, text_data.font);

	// Render each paragraph
	for (const auto& paragraph : text_data.paragraphs) {
		// Render all lines in this paragraph
		for (const auto& line : paragraph.lines) {
			render::text(text_pos, adjusted_colour, line, text_data.font, FONT_CENTERED_X);
			text_pos.y += line_height;
		}

		// Add paragraph spacing after each paragraph except the last
		if (!paragraph.is_last) {
			text_pos.y += PARAGRAPH_SPACING;
		}
	}
}

ui::AnimatedElement* ui::add_hint(
	const std::string& id,
	Container& container,
	const std::vector<std::string>& paragraphs,
	gfx::Color color,
	const render::Font& font
) {
	auto wrapped_paragraphs = wrap_paragraphs(container, paragraphs, font);
	int text_height = calculate_total_height(container, wrapped_paragraphs, font);

	Element element(
		id,
		ElementType::HINT,
		gfx::Rect(container.current_position, gfx::Size(container.get_usable_rect().w, text_height) + HINT_PADDING * 2),
		HintElementData{
			.paragraphs = wrapped_paragraphs,
			.color = color,
			.font = font,
		},
		render_hint
	);

	return add_element(container, std::move(element), container.element_gap);
}
