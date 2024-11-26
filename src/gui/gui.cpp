#include "gui.h"

#include "drag_handler.h"
#include "event_handler.h"
#include "renderer.h"
#include "window_manager.h"

#include "ui/utils.h"

void gui::update_vsync() {
#ifdef _WIN32
	HMONITOR screen_handle = (HMONITOR)window->screen()->nativeHandle();
	static HMONITOR last_screen_handle;
#elif defined(__APPLE__)
	void* screen_handle = window->screen()->nativeHandle();
	static void* last_screen_handle;
#else
	int screen_handle = (int)window->screen()->nativeHandle();
	static int last_screen_handle;
#endif

	if (screen_handle != last_screen_handle) {
		const double rate = utils::get_display_refresh_rate(screen_handle);
		vsync_frame_time = float(1.f / (rate + VSYNC_EXTRA_FPS));
		u::log("switched screen, updated vsync_frame_time. refresh rate: {:.2f} hz", rate);
		last_screen_handle = screen_handle;
	}
}

void gui::event_loop() {
	bool to_render = true;

	while (!closing) {
		auto frame_start = std::chrono::steady_clock::now();

		update_vsync();

		const bool rendered = renderer::redraw_window(
			window.get(), to_render
		); // note: rendered isn't true if rendering was forced, it's only if an animation or smth is playing
		to_render = event_handler::generate_messages_from_os_events(rendered); // true if input handled

#if DEBUG_RENDER && DEBUG_RENDER_LOGGING
		u::log("rendered: {}, to render: {}", rendered, to_render);
#endif

		if (rendered || to_render) {
			auto target_time = frame_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
												 std::chrono::duration<float>(vsync_frame_time)
											 );
			std::this_thread::sleep_until(target_time);
		}
	}
}

void gui::run() {
	auto system = os::make_system();

	renderer::init_fonts();

	system->setAppMode(os::AppMode::GUI);
	system->handleWindowResize = window_manager::on_resize;

	drag_handler::DragTarget drag_target;
	window = window_manager::create_window(drag_target);

	system->finishLaunching();
	system->activateApp();

	base::SystemConsole system_console;
	system_console.prepareShell();

	event_queue = system->eventQueue(); // todo: move this maybe

	event_loop();
}
