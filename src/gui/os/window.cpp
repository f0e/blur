#include "window.h"

#ifndef __APPLE__

bool os::window::disable_live_resize_scaling(SDL_Window* window) {
	(void)window;
	return true;
}

#endif
