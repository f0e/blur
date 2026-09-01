#include "font.h"

#include <imgui.h>
#include <misc/freetype/imgui_freetype.h>
#include <imgui_internal.h>

bool render::Font::init(std::span<const unsigned char> data, float size, ImFontConfig* font_cfg) {
	ImGuiIO* io = &ImGui::GetIO();
	m_size = size;

	ImFontConfig cfg = ImFontConfig();
	if (!font_cfg)
		font_cfg = &cfg;

	font_cfg->FontDataOwnedByAtlas = false;

	m_font = io->Fonts->AddFontFromMemoryCompressedTTF(data.data(), data.size(), m_size, font_cfg);

	return m_font != nullptr;
}

gfx::Size render::Font::calc_size(const std::string& text) const {
	if (!m_font)
		return {};

	auto size = m_font->CalcTextSizeA(m_size, FLT_MAX, 0.f, text.c_str());
	return { (int)size.x, (int)size.y };
}

gfx::Rect render::Font::calc_ink_bounds(const std::string& text) const {
	if (!m_font)
		return {};

	ImFontBaked* baked = m_font->GetFontBaked(m_size);
	if (!baked)
		return {};

	const float scale = m_size / baked->Size;

	float pen_x = 0.f;
	float pen_y = 0.f;

	bool any = false;
	float min_x = 0.f;
	float min_y = 0.f;
	float max_x = 0.f;
	float max_y = 0.f;

	const char* s = text.c_str();
	const char* end = s + text.size();

	while (s < end) {
		unsigned int c = (unsigned int)*s;
		if (c < 0x80)
			s++;
		else
			s += ImTextCharFromUtf8(&c, s, end);

		if (c == '\r')
			continue;

		if (c == '\n') {
			pen_x = 0.f;
			pen_y += m_size;
			continue;
		}

		const ImFontGlyph* glyph = baked->FindGlyph((ImWchar)c);
		if (!glyph)
			continue;

		if (glyph->Visible) {
			float x1 = pen_x + (glyph->X0 * scale);
			float x2 = pen_x + (glyph->X1 * scale);
			float y1 = pen_y + (glyph->Y0 * scale);
			float y2 = pen_y + (glyph->Y1 * scale);

			if (!any) {
				min_x = x1;
				max_x = x2;
				min_y = y1;
				max_y = y2;
				any = true;
			}
			else {
				min_x = std::min(min_x, x1);
				max_x = std::max(max_x, x2);
				min_y = std::min(min_y, y1);
				max_y = std::max(max_y, y2);
			}
		}

		pen_x += glyph->AdvanceX * scale;
	}

	if (!any)
		return {};

	int x = (int)std::lround(min_x);
	int y = (int)std::lround(min_y);

	return { x, y, (int)std::lround(max_x) - x, (int)std::lround(max_y) - y };
}

float render::Font::calc_width(const char* begin, const char* end) const {
	if (!m_font || begin >= end)
		return 0.f;

	return m_font->CalcTextSizeA(m_size, FLT_MAX, 0.f, begin, end).x;
}
