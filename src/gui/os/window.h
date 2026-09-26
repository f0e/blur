#pragma once

struct SDL_Window;

namespace os::window {
	// stops the compositor stretching the last frame while live resizing. returns false if it couldn't be applied
	bool disable_live_resize_scaling(SDL_Window* window);
}
