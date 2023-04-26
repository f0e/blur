#include "font.h"

#include <imgui/imgui.h>
#include <imgui/imgui_freetype.h>

bool drawing::font::init(std::string_view path, float size, const ImFontConfig* font_cfg, const unsigned int* glyph_ranges) {
	ImGuiIO* io = &ImGui::GetIO();
	m_size = size;

	m_font = io->Fonts->AddFontFromFileTTF(path.data(), m_size, font_cfg, glyph_ranges);

	m_height = calc_size("Q").h;

	return m_initialized = m_font;
}

bool drawing::font::init(unsigned char* data, size_t data_size, float size, ImFontConfig* font_cfg, const unsigned int* glyph_ranges) {
	ImGuiIO* io = &ImGui::GetIO();
	m_size = size;

	ImFontConfig cfg = ImFontConfig();
	if (!font_cfg)
		font_cfg = &cfg;

	font_cfg->FontDataOwnedByAtlas = false;

	m_font = io->Fonts->AddFontFromMemoryTTF(data, data_size, m_size, font_cfg, glyph_ranges);

	m_height = calc_size("Q").h;

	return m_initialized = m_font;
}

s_size drawing::font::calc_size(const std::string& text) const {
	auto size = m_font->CalcTextSizeA(m_size, FLT_MAX, 0.f, text.c_str());
	return {
		(int)size.x,
		(int)size.y
	};
}