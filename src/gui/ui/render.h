#pragma once

#include "os/font.h"

namespace render {
	void rect_filled(os::Surface* surface, gfx::Rect rect, gfx::Color colour);
	void rounded_rect_filled(os::Surface* surface, gfx::Rect rect, gfx::Color colour, float rounding);
	void text(os::Surface* surface, gfx::Point pos, gfx::Color colour, std::string text, const SkFont& font, os::TextAlign align = os::TextAlign::Left);

	gfx::Size get_text_size(std::string text, const SkFont& font);
}
