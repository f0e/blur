#pragma once

struct SDL_Window;
union SDL_Event;

namespace os::window {
	struct ScrollEvent {
		float x = 0.f;
		float y = 0.f;
		bool precise = false;
		bool physical = false;
		bool momentum = false;
	};

	bool initialise_scroll_tracking();
	void cleanup_scroll_tracking();
	bool consume_native_scroll_event(const SDL_Event& event, ScrollEvent& scroll);

	// while a window's being live resized the compositor reuses the last frame we drew until a new one turns up,
	// and by default it stretches that frame to whatever size the window is now. this asks it not to.
	// returns false if the platform exposes the setting but it couldn't be applied
	bool disable_live_resize_scaling(SDL_Window* window);
}
