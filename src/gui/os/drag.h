#pragma once

#include <filesystem>

struct SDL_Window;

namespace os::drag {
	// whether files can be dragged out of the window at all. linux has nothing behind it, so rows there stay
	// click-only
	[[nodiscard]] bool supported();

	// hand a file to whatever the user drops it on, the same as dragging it out of a file browser.
	// call it while the mouse button that started the drag is still held - the drop ends the drag.
	// the mouse is taken over for the duration (on windows this runs its own event loop and doesn't return until
	// the file has been dropped), so the button coming back up never reaches us - see keys::forget_mouse_buttons
	bool begin_file_drag(SDL_Window* window, const std::filesystem::path& path);
}
