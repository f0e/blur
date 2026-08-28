#pragma once

struct SDL_Window;

namespace os::taskbar {
	enum class ProgressState : std::uint8_t {
		NONE,          // nothing drawn on the icon
		INDETERMINATE, // busy, but we don't know how far along yet
		NORMAL,
		PAUSED,
		ERRORED, // not ERROR - that's a windows macro
	};

	// binds to the window whose icon gets drawn on. no-op on platforms with nothing to draw on
	void initialise(SDL_Window* window);
	void cleanup();

	// progress is 0-1 and only means anything for NORMAL/PAUSED/ERRORED. calls that wouldn't change
	// what's on screen are dropped, so this is cheap to call every tick
	void set_progress(ProgressState state, float progress = 0.f);
}
