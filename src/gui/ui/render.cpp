#include "render.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "os/skia/skia_surface.h"

// #include "include/core/SkFontMetrics.h"

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

		auto* canvas = &static_cast<os::SkiaSurface*>(surface)->canvas();
		canvas->drawRRect(rrect, paint.skPaint());
	}
}

void render::rect_filled(os::Surface* surface, const gfx::Rect& rect, gfx::Color colour) {
	os::Paint paint;
	paint.color(colour);

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

gfx::Size render::get_text_size(const std::string& text, const SkFont& font) {
	// Skia paint object to calculate text metrics
	SkPaint paint;

	// Get the width of the text
	SkScalar text_width = font.measureText(text.c_str(), text.size(), SkTextEncoding::kUTF8);

	// // Get the text metrics to calculate the height
	// SkFontMetrics metrics;
	// font.getMetrics(&metrics);
	// SkScalar textHeight = metrics.fBottom - metrics.fTop; // Total height of the text (including leading)

	// The result will be a width and height structure
	return gfx::Size(SkScalarToFloat(text_width), SkScalarToFloat(font.getSize()));
}
