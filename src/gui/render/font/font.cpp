#include "font.h"

#include <imgui.h>
#include <misc/freetype/imgui_freetype.h>

bool render::Font::init(std::span<const unsigned char> data, float size, ImFontConfig* font_cfg) {
	ImGuiIO* io = &ImGui::GetIO();
	m_size = size;

	ImFontConfig cfg = ImFontConfig();
	if (!font_cfg)
		font_cfg = &cfg;

	font_cfg->FontDataOwnedByAtlas = false;

	m_font = io->Fonts->AddFontFromMemoryCompressedTTF(data.data(), data.size(), m_size, font_cfg);

	m_height = calc_size("Q").h;

	m_initialised = m_font != nullptr;
	return m_initialised;
}

gfx::Size render::Font::calc_size(const std::string& text) const {
	auto size = m_font->CalcTextSizeA(m_size, FLT_MAX, 0.f, text.c_str());
	return { (int)size.x, (int)size.y };
}
