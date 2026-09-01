#pragma once

#include <imgui.h>

namespace render {
	// a typeface at a specific size. imgui rasterises glyphs on demand, so grabbing the same typeface at a
	// different size is free - just call it, e.g. fonts::dejavu(fonts::size::SMALL)
	class Font {
	private:
		ImFont* m_font{};
		float m_size{};
		bool m_ink_aligned{};

	public:
		bool init(std::span<const unsigned char> data, float size, ImFontConfig* font_cfg = nullptr);

		// align this font by its ink rather than its metrics. icon glyphs aren't centred within their
		// advance, so metric alignment visibly offsets them
		void set_ink_aligned(bool ink_aligned) {
			m_ink_aligned = ink_aligned;
		}

		[[nodiscard]] bool ink_aligned() const {
			return m_ink_aligned;
		}

		[[nodiscard]] Font operator()(float size) const {
			Font resized = *this;
			resized.m_size = size;
			return resized;
		}

		[[nodiscard]] gfx::Size calc_size(const std::string& text) const;

		// bounding box of the pixels a string actually draws, relative to where it would be drawn from
		[[nodiscard]] gfx::Rect calc_ink_bounds(const std::string& text) const;

		// float-precision width of a substring. calc_size truncates to int, which is fine for laying a single
		// string out but drifts once widths are summed
		[[nodiscard]] float calc_width(const char* begin, const char* end) const;

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
