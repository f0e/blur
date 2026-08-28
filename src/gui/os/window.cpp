#include "window.h"

#ifndef __APPLE__

bool os::window::initialise_scroll_tracking() {
	return true;
}

void os::window::cleanup_scroll_tracking() {}

bool os::window::consume_native_scroll_event(const SDL_Event& event, ScrollEvent& scroll) {
	(void)event;
	(void)scroll;
	return false;
}

bool os::window::disable_live_resize_scaling(SDL_Window* window) {
	(void)window;
	return true;
}

#endif
