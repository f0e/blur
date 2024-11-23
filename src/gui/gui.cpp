#include "gui.h"
#include "gfx/color.h"
#include "gfx/rgb.h"
#include "gui/ui/render.h"
#include "os/draw_text.h"
#include "tasks.h"
#include "easing.h"

#include "ui/ui.h"
#include "ui/utils.h"

#include "resources/font.h"

const int font_height = 14;
const int pad_x = 24;
const int pad_y = 35;

bool closing = false;

SkFont font;
os::EventQueue* event_queue;

std::chrono::high_resolution_clock::time_point last_frame_time = std::chrono::high_resolution_clock::now();

void gui::DragTarget::dragEnter(os::DragEvent& ev) {
	// v.dropResult(os::DropOperation::None); // TODO: what does this do? is it needed?

	windowData.dragPosition = ev.position();
	windowData.dragging = true;

	redraw_window(ev.target());
}

void gui::DragTarget::dragLeave(os::DragEvent& ev) {
	// todo: not triggering on windows?
	windowData.dragPosition = ev.position();
	windowData.dragging = false;

	redraw_window(ev.target());
}

void gui::DragTarget::drag(os::DragEvent& ev) {
	windowData.dragPosition = ev.position();

	redraw_window(ev.target());
}

void gui::DragTarget::drop(os::DragEvent& ev) {
	ev.acceptDrop(true);

	if (ev.dataProvider()->contains(os::DragDataItemType::Paths)) {
		std::vector<std::string> paths = ev.dataProvider()->getPaths();
		tasks::add_files(paths);
	}

	windowData.dragging = false;

	redraw_window(ev.target());
}

static os::WindowRef create_window(os::DragTarget& dragTarget) {
	auto screen = os::instance()->mainScreen();

	os::WindowRef window = os::instance()->makeWindow(591, 381);
	window->setCursor(os::NativeCursor::Arrow);
	window->setTitle("Blur");
	window->setDragTarget(&dragTarget);

	gui::windowData.dropZone = gfx::Rect(
		0,
		0,
		window->width(),
		window->height()
	);

	gui::redraw_window(window.get());
	return window;
}

bool processEvent(const os::Event& ev) {
	switch (ev.type()) {
		case os::Event::KeyDown:
			if (ev.scancode() == os::kKeyEsc)
				return false;
			break;

		case os::Event::CloseApp:
		case os::Event::CloseWindow:
			closing = true;
			return false;

		case os::Event::ResizeWindow:
			gui::redraw_window(ev.window().get());
			break;

		default:
			break;
	}

	return true;
}

void gui::generate_messages_from_os_events() { // https://github.com/aseprite/aseprite/blob/45c2a5950445c884f5d732edc02989c3fb6ab1a6/src/ui/manager.cpp#L393
	assert(event_queue);

	os::Event lastMouseMoveEvent;

	os::Event event;
	while (true) {
		// Calculate how much time we can wait for the next message in the
		// event queue.
		double timeout = 0.0;
		// if (msg_queue.empty()) {
		// 	if (!Timer::getNextTimeout(timeout)) // nothing on timer so just wait forever i guess
		// 		timeout = os::EventQueue::kWithoutTimeout;
		// }

		event_queue->getEvent(event, timeout);
		if (event.type() == os::Event::None)
			break;

		processEvent(event);
	}
}

void gui::event_loop() {
	while (!closing) {
		redraw_window(window.get());
		generate_messages_from_os_events();
	}
}

void gui::redraw_window(os::Window* window) {
	static bool is_first_frame = true;
	auto now = std::chrono::high_resolution_clock::now();

	std::chrono::duration<float> delta_time_duration = now - last_frame_time;

	float delta_time;
	if (!is_first_frame) {
		std::chrono::duration<float> delta_time_duration = now - last_frame_time;
		delta_time = delta_time_duration.count();
	}
	else {
		delta_time = 0.0f;
		is_first_frame = false;
	}

	last_frame_time = now;

	//

	os::Surface* s = window->surface();
	const gfx::Rect rc = s->bounds();

	os::Paint text_paint;
	text_paint.color(gfx::rgba(255, 255, 255, 255));

	// background
	os::Paint paint;
	paint.color(gfx::rgba(0, 0, 0, 255));
	s->drawRect(rc, paint);

	{
		// debug
		static const int debug_box_size = 30;
		static float x = 0.f, y = 0.f;
		static bool right = true;
		static bool down = true;
		x += 500.f * delta_time * (right ? 1 : -1);
		y += 500.f * delta_time * (down ? 1 : -1);
		os::Paint paint;
		paint.color(gfx::rgba(255, 0, 0, 50));
		s->drawRect(gfx::Rect(x, y, debug_box_size, debug_box_size), paint);

		if (right) {
			if (x + debug_box_size > rc.x2())
				right = false;
		}
		else {
			if (x < 0)
				right = true;
		}

		if (down) {
			if (y + debug_box_size > rc.y2())
				down = false;
		}
		else {
			if (y < 0)
				down = true;
		}
	}

	gfx::Rect drop_zone = rc;

	static float fill_shade = 10.f;
	fill_shade = std::lerp(fill_shade, windowData.dragging ? 30.f : 10.f, 25.f * delta_time);
	paint.color(gfx::rgba(255, 255, 255, fill_shade));
	s->drawRect(drop_zone, paint);

	// paint.style(os::Paint::Style::Stroke);
	// const int stroke_shade = 100;
	// const int stroke_width = 2;
	// paint.color(gfx::rgba(255, 255, 255, stroke_shade));
	// paint.strokeWidth(stroke_width);
	// s->drawRect(drop_zone, paint);

	// const int blur_stroke_width = 1;
	// const float blur_start_shade = 50;
	// const float blur_screen_percent = 0.8f;

	// paint.style(os::Paint::Style::Stroke);
	// paint.strokeWidth(blur_stroke_width);

	// gfx::Rect blur_drop_zone = drop_zone;
	// const int blur_steps = (std::min(rc.w, rc.h) / 2.f / blur_stroke_width) * blur_screen_percent;

	// for (int i = 0; i < blur_steps; i++) {
	// 	blur_drop_zone.shrink(blur_stroke_width);
	// 	int shade = std::lerp(blur_start_shade, 0, ease_out_quart(i / (float)blur_steps));
	// 	if (shade <= 0)
	// 		break;

	// 	paint.color(gfx::rgba(255, 255, 255, shade));
	// 	s->drawRect(blur_drop_zone, paint);
	// }

	gfx::Rect container_rect = rc;
	container_rect.x += pad_x;
	container_rect.w -= pad_x * 2;
	container_rect.y += pad_y;
	container_rect.h -= pad_y * 2;

	auto container = ui::create_container(container_rect);

	if (rendering.queue.empty()) {
		ui::add_text(container, "Drop a file...", gfx::rgba(255, 255, 255, 255), font, os::TextAlign::Center);
	}
	else {
		for (const auto [i, render] : helpers::enumerate(rendering.queue)) {
			bool current = render == rendering.current_render;

			ui::add_text(container, base::to_utf8(render->get_video_name()), gfx::rgba(255, 255, 255, (current ? 255 : 100)), font, os::TextAlign::Center);

			if (current) {
				auto render_status = render->get_status();

				std::string preview_path = render->get_preview_path().string();
				if (preview_path != "") {
					ui::add_image(container, preview_path, gfx::Size(container_rect.w, 200));
				}

				if (render_status.init) {
					ui::add_text(container, render_status.progress_string, gfx::rgba(255, 255, 255, 255), font, os::TextAlign::Center);
					ui::add_bar(container, render_status.current_frame / (float)render_status.total_frames, gfx::rgba(51, 51, 51, 255), gfx::rgba(255, 255, 255, 255));
				}
				else {
					ui::add_text(container, "initialising render...", gfx::rgba(255, 255, 255, 255), font, os::TextAlign::Center);
				}
			}
		}
	}

	ui::center_elements_in_container(container);

	ui::render_container(s, container, 1.0f);

	if (!window->isVisible())
		window->setVisible(true);

	window->invalidateRegion(gfx::Region(rc));
}

void gui::run() {
	auto system = os::make_system();

	font = utils::create_font_from_data(ttf_FiraCode_Regular, ttf_FiraCode_Regular_len, font_height);

	system->setAppMode(os::AppMode::GUI);
	system->handleWindowResize = redraw_window;

	DragTarget dragTarget;
	window = create_window(dragTarget);

	system->finishLaunching();
	system->activateApp();

	event_queue = system->eventQueue();

	event_loop();
}
