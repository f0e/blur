
#pragma once

namespace gui::renderer {
	namespace fonts {
		inline SkFont font;
		inline SkFont header_font;
		inline SkFont smaller_header_font;
	}

	inline os::NativeCursor current_cursor;
	inline bool set_cursor_this_frame = false;

	void init_fonts();

	void set_cursor(os::NativeCursor cursor);

	bool redraw_window(os::Window* window, bool force_render);
}
