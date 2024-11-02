#include "gui.h"
#include "tasks.h"

void gui::DragTarget::dragEnter(os::DragEvent& ev) {
	// v.dropResult(os::DropOperation::None); // TODO: what does this do? is it needed?

	windowData.dragPosition = ev.position();
	windowData.dragging = true;

	redraw_window(ev.target());
}

void gui::DragTarget::dragLeave(os::DragEvent& ev) {
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

	os::WindowRef window = os::instance()->makeWindow(800, 600);
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
			return false;

		case os::Event::ResizeWindow:
			gui::redraw_window(ev.window().get());
			break;

		default:
			break;
	}

	return true;
}

void gui::redraw_window(os::Window* window) {
	os::Surface* s = window->surface();
	const gfx::Rect rc = s->bounds();

	os::Paint paint;

	// black background
	paint.color(gfx::rgba(0, 0, 0, 255));
	s->drawRect(rc, paint);

	const static int spacing = 20;
	const static int pad_x = 25;
	const static int pad_y = 35;
	int y = 0;
	auto draw_str_temp = [&y, &s, &paint](std::string text, gfx::Color colour = gfx::rgba(255, 255, 255, 255)) {
		paint.color(colour);

		// todo: clip string
		os::draw_text(s, nullptr, text, gfx::Point(pad_x, pad_y + y), &paint);

		y += spacing;
	};

	draw_str_temp("blur");
	draw_str_temp("drop a file...");

	if (windowData.dragging) {
		draw_str_temp("drop me bro");
	}

	if (!rendering.queue.empty()) {
		for (const auto [i, render] : helpers::enumerate(rendering.queue)) {
			bool current = render == rendering.current_render;

			std::string name_str = helpers::tostring(render->get_video_name());
			draw_str_temp(name_str, gfx::rgba(255, 255, 255, (current ? 255 : 100)));

			if (current) {
				auto render_status = render->get_status();

				if (render_status.init) {
					std::string progress_string = render_status.progress_string();

					draw_str_temp(progress_string);
				}
				else {
					draw_str_temp("initialising render...");
				}
			}
		}
	}

	if (!window->isVisible())
		window->setVisible(true);

	window->invalidateRegion(gfx::Region(rc));
}

void gui::run() {
	auto system = os::make_system();
	system->setAppMode(os::AppMode::GUI);
	system->handleWindowResize = redraw_window;

	DragTarget dragTarget;
	window = create_window(dragTarget);

	system->finishLaunching();
	system->activateApp();

	os::EventQueue* queue = system->eventQueue();
	os::Event ev;

	while (true) {
		if (queue_redraw) {
			redraw_window(window.get());
			queue_redraw = false;
		}

		queue->getEvent(ev); // NOTE: blocking

		if (!processEvent(ev))
			break;
	}
}
