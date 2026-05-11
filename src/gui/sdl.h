#pragma once

// need to include this shit cause this is also included from .mm file which doesnt get pch
#include <tl/expected.hpp>
#include "render/primitives/size.h"

namespace sdl {
	inline SDL_Window* window = nullptr;
	inline SDL_GLContext gl_context = nullptr;

	inline constexpr gfx::Size MINIMUM_WINDOW_SIZE(450, 250);

	inline const float VSYNC_EXTRA_FPS = 50;
	inline const float MIN_FPS = 10.f;
	inline const float DEFAULT_DELTA_TIME = 1.f / 60;
	inline double vsync_frame_time_ms = DEFAULT_DELTA_TIME * 1000.f;
	inline const float TICKRATE_MS = 1.f / 20 * 1000.f;

	tl::expected<void, std::string> initialise();
	void cleanup();

	bool event_watcher(void* data, SDL_Event* event);

	void on_frame_start();
	void set_cursor(SDL_SystemCursor cursor);
	void update_vsync();
}
