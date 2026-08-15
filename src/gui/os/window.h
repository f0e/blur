#pragma once

struct SDL_Window;

namespace os::window {
	// while a window's being live resized the compositor reuses the last frame we drew until a new one turns up,
	// and by default it stretches that frame to whatever size the window is now. this asks it not to.
	// returns false if the platform exposes the setting but it couldn't be applied
	bool disable_live_resize_scaling(SDL_Window* window);
}
