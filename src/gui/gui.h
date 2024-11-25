#pragma once

namespace gui {
	inline os::WindowRef window;
	inline os::EventQueue* event_queue;

	inline bool closing = false;

	const inline float vsync_extra_fps = 50;
	const inline float min_delta_time = 1.f / 10;
	const inline float default_delta_time = 1.f / 60;
	inline float vsync_frame_time = default_delta_time;

	void update_vsync();

	void event_loop();
	void run();
}
