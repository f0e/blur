#pragma once

#include <imgui.h>

namespace render {
	// a typeface at a specific size. imgui rasterises glyphs on demand, so grabbing the same typeface at a
	// different size is free - just call it, e.g. fonts::dejavu(fonts::size::SMALL)
	class Font {
	private:
		ImFont* m_font{};
		float m_size{};

	public:
		bool init(std::span<const unsigned char> data, float size, ImFontConfig* font_cfg = nullptr);

		[[nodiscard]] Font operator()(float size) const {
			Font resized = *this;
			resized.m_size = size;
			return resized;
		}

		[[nodiscard]] gfx::Size calc_size(const std::string& text) const;

		[[nodiscard]] int height() const {
			return calc_size("Q").h;
		}

		[[nodiscard]] ImFont* im_font() const {
			return m_font;
		}

		[[nodiscard]] float size() const {
			return m_size;
		}

		operator bool() const {
			return m_font != nullptr;
		}

		bool operator==(const Font& other) const = default;
	};
}
