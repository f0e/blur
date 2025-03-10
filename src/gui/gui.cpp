#include "gui.h"

#include "drag_handler.h"
#include "event_handler.h"
#include "renderer.h"
#include "window_manager.h"

#include "ui/utils.h"

#define DEBUG_RENDER_LOGGING 0

float scale = 1.f;

void gui::update_vsync() {
#ifdef _WIN32
	HMONITOR screen_handle = (HMONITOR)window->screen()->nativeHandle();
	static HMONITOR last_screen_handle;
#elif defined(__APPLE__)
	void* screen_handle = window->screen()->nativeHandle();
	static void* last_screen_handle;
#else
	intptr_t screen_handle = (intptr_t)window->screen()->nativeHandle();
	static intptr_t last_screen_handle;
#endif

	if (screen_handle != last_screen_handle) {
		const double rate = utils::get_display_refresh_rate(screen_handle);
		vsync_frame_time = float(1.f / (rate + VSYNC_EXTRA_FPS));
		u::log("switched screen, updated vsync_frame_time. refresh rate: {:.2f} hz", rate);
		last_screen_handle = screen_handle;
	}
}

void gui::event_loop() {
	bool rendered_last = false;

	while (!closing) {
		auto frame_start = std::chrono::steady_clock::now();

		update_vsync();

		// update dpi scaling
		float new_scale = utils::get_display_scale_factor();
		if (new_scale != scale) {
			// window->setScale(new_scale);
			scale = new_scale;
		}

		to_render |= event_handler::handle_events(rendered_last); // true if input handled

		const bool rendered = renderer::redraw_window(
			window.get(), to_render
		); // note: rendered isn't true if rendering was forced, it's only if an animation or smth is playing

#if DEBUG_RENDER_LOGGING
		u::log("rendered: {}, to render: {}", rendered, to_render);
#endif

		// vsync
		if (rendered || to_render) {
			to_render = false;
			rendered_last = true;

#ifdef WIN32
			u::sleep(vsync_frame_time); // killllll windowwssssss
#else
			auto target_time = frame_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
												 std::chrono::duration<float>(vsync_frame_time)
											 );
			std::this_thread::sleep_until(target_time);
#endif
		}
		else {
			rendered_last = false;
		}
	}
}

void gui::run() {
	auto system = os::make_system(); // note: storing this and not calling release() causes skia crash

	renderer::init_fonts();

	system->setAppMode(os::AppMode::GUI);
	system->handleWindowResize = window_manager::on_resize;

	drag_handler::DragTarget drag_target;
	window = window_manager::create_window(drag_target);

	system->finishLaunching();
	system->activateApp();

	scale = utils::get_display_scale_factor();
	// window->setScale(scale);

	event_queue = system->eventQueue(); // todo: move this maybe

	event_loop();
}
