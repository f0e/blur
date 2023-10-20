#pragma once

struct ImFont;
struct ImFontConfig;

namespace drawing {
	struct font {
	private:
		ImFont* m_font;
		float m_size;
		bool m_initialized = false;
		int m_height = 0;

	public:
		bool init(const std::string& path, float size, const ImFontConfig* font_cfg, const ImWchar* glyph_ranges);
		bool init(unsigned char* data, size_t data_size, float size, ImFontConfig* font_cfg, const ImWchar* glyph_ranges);

		s_size calc_size(const std::string& text) const;

		ImFont* im_font() const {
			return m_font;
		}

		float size() const {
			return m_size;
		}

		operator bool() const {
			return m_initialized;
		}

		int height() const {
			return m_height;
		}
	};
}