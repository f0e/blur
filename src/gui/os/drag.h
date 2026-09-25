#pragma once

#include <filesystem>

struct SDL_Window;

namespace os::drag {
	[[nodiscard]] bool supported();

	// call while the mouse button is still held. the button release never reaches us, see keys::forget_mouse_buttons
	bool begin_file_drag(SDL_Window* window, const std::filesystem::path& path);
}
