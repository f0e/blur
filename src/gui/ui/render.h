#pragma once

namespace render {
	void rect_filled(os::Surface* surface, const gfx::Rect& rect, gfx::Color colour);
	void rounded_rect_filled(os::Surface* surface, const gfx::Rect& rect, gfx::Color colour, float rounding);
	void rounded_rect_stroke(
		os::Surface* surface, const gfx::Rect& rect, gfx::Color colour, float rounding, float stroke_width = 1
	);
	void text(
		os::Surface* surface,
		const gfx::Point& pos,
		gfx::Color colour,
		const std::string& text,
		const SkFont& font,
		os::TextAlign align = os::TextAlign::Left
	);

	gfx::Size get_text_size(const std::string& text, const SkFont& font);
	std::vector<std::string> wrap_text(
		const std::string& text, const gfx::Size& dimensions, const SkFont& font, int line_height = 0
	);
}
