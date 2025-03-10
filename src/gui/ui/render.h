#pragma once

namespace render {
	void rect_filled(os::Surface* surface, const gfx::Rect& rect, gfx::Color colour);
	void rect_stroke(os::Surface* surface, const gfx::Rect& rect, gfx::Color colour, float stroke_width = 1.f);

	void rounded_rect_filled(os::Surface* surface, const gfx::Rect& rect, gfx::Color colour, float rounding);
	void rounded_rect_stroke(
		os::Surface* surface, const gfx::Rect& rect, gfx::Color colour, float rounding, float stroke_width = 1.f
	);

	enum class GradientDirection : uint8_t {
		GRADIENT_LEFT,
		GRADIENT_RIGHT,
		GRADIENT_UP,
		GRADIENT_DOWN
	};

	void rect_filled_gradient(
		os::Surface* surface,
		const gfx::Rect& rect,
		const std::vector<gfx::Point>& gradient_direction,
		const std::vector<gfx::Color>& colors,
		const std::vector<float>& positions,
		SkTileMode tile_mode = SkTileMode::kMirror
	);

	void rect_filled_gradient(
		os::Surface* surface,
		const gfx::Rect& rect,
		GradientDirection direction,
		const std::vector<gfx::Color>& colors,
		const std::vector<float>& positions = { 0.f, 1.f },
		SkTileMode tile_mode = SkTileMode::kMirror
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
	int get_font_height(const SkFont& font);

	std::vector<std::string> wrap_text(
		const std::string& text, const gfx::Size& dimensions, const SkFont& font, int line_height = 0
	);
}
