
#pragma once

#include "common/rendering.h"

namespace ui {
	class Container;
}

namespace gui::renderer {
	namespace fonts {
		inline SkFont font;
		inline SkFont header_font;
		inline SkFont smaller_header_font;
	}

	inline os::NativeCursor current_cursor;
	inline bool set_cursor_this_frame = false;

	inline std::optional<Render> current_render_copy;

	void init_fonts();

	void set_cursor(os::NativeCursor cursor);

	namespace components {
		void render_in_progress(ui::Container& container, const Render* render, float& bar_percent, float& delta_time);

	}

	bool redraw_window(os::Window* window, bool force_render);
}
