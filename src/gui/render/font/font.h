#pragma once

#include <imgui.h>

namespace render {
	struct Font {
	private:
		ImFont* m_font{};
		float m_size{};
		bool m_initialised = false;
		int m_height = 0;

	public:
		bool init(std::span<const unsigned char> data, float size, ImFontConfig* font_cfg);

		[[nodiscard]] gfx::Size calc_size(const std::string& text) const;

		[[nodiscard]] ImFont* im_font() const {
			return m_font;
		}

		[[nodiscard]] float size() const {
			return m_size;
		}

		operator bool() const {
			return m_initialised;
		}

		[[nodiscard]] int height() const {
			return m_height;
		}
	};
}
