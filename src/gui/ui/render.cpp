#include "render.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "os/skia/skia_surface.h"

// todo: is creating a new paint instance every time significant to perf? shouldnt be

void render::rect_filled(os::Surface* surface, gfx::Rect rect, gfx::Color colour) {
	os::Paint paint;
	paint.color(colour);

	surface->drawRect(rect, paint);
}

void render::rounded_rect_filled(os::Surface* surface, gfx::Rect rect, gfx::Color colour, float rounding) {
	if (rect.isEmpty())
		return;

	os::Paint paint;
	paint.color(colour);

	auto skia_rect = SkRect::MakeXYWH(SkScalar(rect.x), SkScalar(rect.y), SkScalar(rect.w), SkScalar(rect.h));

	SkRRect rrect;
	rrect.setRectXY(skia_rect, rounding, rounding);

	auto canvas = &static_cast<os::SkiaSurface*>(surface)->canvas();
	canvas->drawRRect(rrect, paint.skPaint());
}

void render::text(os::Surface* surface, gfx::Point pos, gfx::Color colour, std::string text, const SkFont& font, os::TextAlign align) {
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

gfx::Size render::get_text_size(std::string text, const SkFont& font) {
	// Skia paint object to calculate text metrics
	SkPaint paint;

	// Get the width of the text
	SkScalar textWidth = font.measureText(text.c_str(), text.size(), SkTextEncoding::kUTF8);

	// // Get the text metrics to calculate the height
	// SkFontMetrics metrics;
	// font.getMetrics(&metrics);
	// SkScalar textHeight = metrics.fBottom - metrics.fTop; // Total height of the text (including leading)

	// The result will be a width and height structure
	return gfx::Size(SkScalarToFloat(textWidth), SkScalarToFloat(font.getSize()));
}
