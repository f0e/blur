#pragma once

namespace gui {
	inline os::WindowRef window;
	inline os::EventQueue* event_queue;

	inline bool closing = false;
	inline bool to_render = true;

	const inline float VSYNC_EXTRA_FPS = 50;
	const inline float MIN_DELTA_TIME = 1.f / 10;
	const inline float DEFAULT_DELTA_TIME = 1.f / 60;
	inline double vsync_frame_time = DEFAULT_DELTA_TIME;

	void update_vsync();

	void event_loop();
	void run();
}
