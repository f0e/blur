#pragma once

namespace gui {
	inline tl::expected<void, std::string> initialisation_res = tl::make_unexpected("Not initialised");

	inline bool to_render = true;

	inline bool dragging = false;

	// set when a render fails so the taskbar icon can go red, cleared once the window's been looked at again.
	// written from the render queue's thread, read from the gui thread
	inline std::atomic<bool> render_failed = false;

	void event_loop();
	int run();
}
