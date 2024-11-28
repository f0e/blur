#pragma once

namespace render {
	void rect_filled(os::Surface* surface, const gfx::Rect& rect, gfx::Color colour);
	void rect_stroke(os::Surface* surface, const gfx::Rect& rect, gfx::Color colour, float stroke_width = 1.f);

	void rounded_rect_filled(os::Surface* surface, const gfx::Rect& rect, gfx::Color colour, float rounding);
	void rounded_rect_stroke(
		os::Surface* surface, const gfx::Rect& rect, gfx::Color colour, float rounding, float stroke_width = 1.f
	);

	void line(
		os::Surface* surface,
		const gfx::Point& point1,
		const gfx::Point& point2,
		gfx::Color colour,
		float thickness = 1.f
	);

	void text(
		os::Surface* surface,
		const gfx::Point& pos,
		gfx::Color colour,
		const std::string& text,
		const SkFont& font,
		os::TextAlign align = os::TextAlign::Left
	);

	inline std::stack<gfx::Rect> clip_rects;

	void push_clip_rect(os::Surface* surface, const gfx::Rect& rect);
	void pop_clip_rect(os::Surface* surface);

	gfx::Size get_text_size(const std::string& text, const SkFont& font);

	std::vector<std::string> wrap_text(
		const std::string& text, const gfx::Size& dimensions, const SkFont& font, int line_height = 0
	);
}
