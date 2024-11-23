#include "render.h"

// todo: is creating a new paint instance every time significant to perf? shouldnt be

void render::rect_filled(os::Surface* surface, gfx::Rect rect, gfx::Color colour) {
	os::Paint paint;
	paint.color(colour);

	surface->drawRect(rect, paint);
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
