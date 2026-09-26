#pragma once

namespace gui {
	inline tl::expected<void, std::string> initialisation_res = tl::make_unexpected("Not initialised");

	inline bool to_render = true;

	inline bool dragging = false;

	// set when a render fails so the taskbar icon goes red until the window's focused. written from the render thread
	inline std::atomic<bool> render_failed = false;

	void event_loop();
	int run();
}
