#include "render.h"
#include <include/core/SkPaint.h>
#include <include/core/SkRRect.h>
#include <os/skia/skia_surface.h>

#include <include/core/SkFontMetrics.h>
#include <include/effects/SkGradientShader.h>

// todo: is creating a new paint instance every time significant to perf? shouldnt be

namespace {
	void rounded_rect(os::Surface* surface, const gfx::RectF& rect, os::Paint paint, float rounding) {
		if (rect.isEmpty())
			return;

		paint.antialias(true);

		SkRect skia_rect{};
		if (paint.style() == os::Paint::Style::Stroke)
			skia_rect = os::to_skia_fix(rect);
		else
			skia_rect = os::to_skia(rect);

		SkRRect rrect;
		rrect.setRectXY(skia_rect, rounding, rounding);

		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast) aseprite code
		auto* canvas = &static_cast<os::SkiaSurface*>(surface)->canvas();
		canvas->drawRRect(rrect, paint.skPaint());
	}
}

void render::rect_filled(os::Surface* surface, const gfx::Rect& rect, gfx::Color colour) {
	os::Paint paint;
	paint.color(colour);

	surface->drawRect(rect, paint);
}

void render::rect_filled_gradient(
	os::Surface* surface,
	const gfx::Rect& rect,
	const std::vector<gfx::Point>& gradient_direction,
	const std::vector<gfx::Color>& colors,
	const std::vector<float>& positions,
	SkTileMode tile_mode
) {
	if (!surface || gradient_direction.size() != 2 || colors.size() < 2 || colors.size() != positions.size()) {
		return; // Ensure valid input
	}

	// Convert gfx::Point to SkPoint using os::to_skia
	std::vector<SkPoint> skia_points;
	skia_points.reserve(gradient_direction.size());
	for (const auto& point : gradient_direction)
		skia_points.push_back(SkPoint(point.x, point.y));

	// Convert gfx::Color to SkColor using os::to_skia
	std::vector<SkColor> skia_colors;
	skia_colors.reserve(colors.size());
	for (const auto& color : colors)
		skia_colors.push_back(os::to_skia(color));

	// Create gradient shader using the converted vectors
	SkMatrix matrix = SkMatrix::I();
	auto shader = SkGradientShader::MakeLinear(
		skia_points.data(), skia_colors.data(), positions.data(), skia_colors.size(), tile_mode, 0, &matrix
	);

	SkPaint paint;
	paint.setShader(shader);

	// Convert gfx::Rect to SkRect
	SkRect skia_rect = os::to_skia(gfx::RectF(rect));

	// Draw the rectangle on the surface's canvas
	auto* canvas = &static_cast<os::SkiaSurface*>(surface)->canvas();
	canvas->drawRect(skia_rect, paint);
}

void render::rect_filled_gradient(
	os::Surface* surface,
	const gfx::Rect& rect,
	GradientDirection direction,
	const std::vector<gfx::Color>& colors,
	const std::vector<float>& positions,
	SkTileMode tile_mode
) {
	gfx::Point start;
	gfx::Point end;

	switch (direction) {
		case GradientDirection::GRADIENT_LEFT:
			start = gfx::Point(rect.x + rect.w, rect.y);
			end = gfx::Point(rect.x, rect.y);
			break;
		case GradientDirection::GRADIENT_RIGHT:
			start = gfx::Point(rect.x, rect.y);
			end = gfx::Point(rect.x + rect.w, rect.y);
			break;
		case GradientDirection::GRADIENT_UP:
			start = gfx::Point(rect.x, rect.y + rect.h);
			end = gfx::Point(rect.x, rect.y);
			break;
		case GradientDirection::GRADIENT_DOWN:
			start = gfx::Point(rect.x, rect.y);
			end = gfx::Point(rect.x, rect.y + rect.h);
			break;
	}

	std::vector<gfx::Point> gradient_direction = { start, end };

	rect_filled_gradient(surface, rect, gradient_direction, colors, positions, tile_mode);
}

void render::rect_stroke(os::Surface* surface, const gfx::Rect& rect, gfx::Color colour, float stroke_width) {
	os::Paint paint;
	paint.color(colour);
	paint.style(os::Paint::Style::Stroke);
	paint.strokeWidth(stroke_width);

	surface->drawRect(rect, paint);
}

void render::rounded_rect_filled(os::Surface* surface, const gfx::Rect& rect, gfx::Color colour, float rounding) {
	if (rect.isEmpty())
		return;

	os::Paint paint;
	paint.color(colour);

	rounded_rect(surface, rect, paint, rounding);
}

void render::rounded_rect_stroke(
	os::Surface* surface, const gfx::Rect& rect, gfx::Color colour, float rounding, float stroke_width
) {
	if (rect.isEmpty())
		return;

	os::Paint paint;
	paint.color(colour);
	paint.style(os::Paint::Style::Stroke);
	paint.strokeWidth(stroke_width);

	rounded_rect(surface, rect, paint, rounding);
}

void render::line(
	os::Surface* surface, const gfx::Point& point1, const gfx::Point& point2, gfx::Color colour, float thickness
) {
	os::Paint paint;
	paint.color(colour);

	paint.antialias(true);
	paint.strokeWidth(thickness);

	surface->drawLine(point1, point2, paint);
}

void render::text(
	os::Surface* surface,
	const gfx::Point& pos,
	gfx::Color colour,
	const std::string& text,
	const SkFont& font,
	os::TextAlign align
) {
	os::Paint paint;
	paint.color(colour);

	// todo: clip string

	// os::draw_text font broken with skia bruh - need to call skia func directly
	SkTextUtils::Draw(
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast) aseprite code
		&static_cast<os::SkiaSurface*>(surface)->canvas(),
		text.c_str(),
		text.size(),
		SkTextEncoding::kUTF8,
		SkIntToScalar(pos.x),
		SkIntToScalar(pos.y),
		font,
		paint.skPaint(),
		(SkTextUtils::Align)align
	);
}

void render::push_clip_rect(os::Surface* surface, const gfx::Rect& rect) {
	if (clip_rects.empty())
		surface->saveClip();

	clip_rects.push(rect);
	surface->clipRect(rect);
}

void render::pop_clip_rect(os::Surface* surface) {
	assert(!clip_rects.empty() && "mismatched push/pop clip rect");

	clip_rects.pop();

	if (clip_rects.empty()) {
		surface->restoreClip();
		return;
	}

	const auto& rect = clip_rects.top();
	surface->clipRect(rect);
}

// gfx::Size render::get_text_size(const std::string& text, const SkFont& font) {
// 	// Skia paint object to calculate text metrics
// 	SkPaint paint;

// 	// Get the width of the text
// 	SkScalar text_width = font.measureText(text.c_str(), text.size(), SkTextEncoding::kUTF8);

// 	// // Get the text metrics to calculate the height
// 	// SkFontMetrics metrics;
// 	// font.getMetrics(&metrics);
// 	// SkScalar textHeight = metrics.fBottom - metrics.fTop; // Total height of the text (including leading)

// 	// The result will be a width and height structure
// 	return { SkScalarTruncToInt(text_width),
// 		     SkScalarTruncToInt(font.getSize()) }; // todo: SkScalarTruncToInt rounds i think, should i just cast to int
// }

gfx::Size render::get_text_size(const std::string& text, const SkFont& font) {
	// Skia paint object to calculate text metrics
	SkPaint paint;

	// Get the width of the text
	SkScalar text_width = font.measureText(text.c_str(), text.size(), SkTextEncoding::kUTF8);

	// The result will be a width and height structure
	return { SkScalarTruncToInt(text_width), get_font_height(font) };
}

int render::get_font_height(const SkFont& font) {
	SkFontMetrics metrics{};
	font.getMetrics(&metrics);
	// SkScalar text_height = metrics.fBottom - metrics.fTop; // Total height of the text (including leading)
	// float total_height = SkScalarAbs(metrics.fAscent) + metrics.fDescent + metrics.fLeading;
	int text_height = ceil(metrics.fCapHeight); // good enough - todo: maybe should be round

	return text_height;
}

// NOLINTBEGIN(readability-function-size,readability-function-cognitive-complexity) ai code idc
std::vector<std::string> render::wrap_text(
	const std::string& text, const gfx::Size& dimensions, const SkFont& font, int line_height
) {
	std::vector<std::string> lines;
	std::istringstream words_stream(text);
	std::string word;
	std::string current_line;

	int space_width = get_text_size(" ", font).w;
	int total_height = 0;
	int current_line_width = 0;
	bool truncated = false;

	// Precompute ellipsis width
	int ellipsis_width = get_text_size("...", font).w;

	// Use provided line height or fall back to font's height
	auto get_line_height = [&](const std::string& line) {
		return line_height > 0 ? line_height : get_text_size(line, font).h;
	};

	while (words_stream >> word) {
		auto word_size = get_text_size(word, font);
		int new_line_width = current_line_width + (current_line_width > 0 ? space_width : 0) + word_size.w;

		// Check if word fits in current line
		if (new_line_width <= dimensions.w) {
			if (!current_line.empty()) {
				current_line += " ";
			}
			current_line += word;
			current_line_width = new_line_width;
		}
		else {
			// Commit current line
			if (!current_line.empty()) {
				lines.push_back(std::move(current_line));
				total_height += get_line_height(lines.back());
			}

			// Stop if adding another line exceeds height
			if (total_height + get_line_height(word) > dimensions.h) {
				truncated = true;
				break;
			}

			// Start new line
			current_line = word;
			current_line_width = word_size.w;
		}
	}

	// Add last line if it fits
	if (!current_line.empty() && total_height + get_line_height(current_line) <= dimensions.h) {
		lines.push_back(std::move(current_line));
	}

	// If text was truncated, modify the last line to add ellipsis
	if (truncated && !lines.empty()) {
		// Trim the last line to fit ellipsis
		auto& last_line = lines.back();
		while (!last_line.empty()) {
			int current_width = get_text_size(last_line, font).w;
			if (current_width + ellipsis_width <= dimensions.w) {
				last_line += "...";
				break;
			}
			// Remove last word
			auto last_space = last_line.find_last_of(' ');
			if (last_space == std::string::npos) {
				last_line.clear();
				break;
			}
			last_line = last_line.substr(0, last_space);
		}
	}

	return lines;
}

// NOLINTEND(readability-function-size,readability-function-cognitive-complexity)
