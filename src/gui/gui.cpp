#include "gui.h"
#include "tasks.h"
#include "gui_helpers.h"

#include "resources/font.h"

const int font_height = 14;
const int spacing = 4;
const int pad_x = 24;
const int pad_y = 35;

SkFont font;

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
			return false;

		case os::Event::ResizeWindow:
			gui::redraw_window(ev.window().get());
			break;

		default:
			break;
	}

	return true;
}

void draw_text(os::Surface* s, const os::Paint& paint, gfx::Point pos, std::string text, os::TextAlign textAlign) {
	// todo: clip string
	// os::draw_text font broken with skia bruh
	SkTextUtils::Draw(
		&static_cast<os::SkiaSurface*>(s)->canvas(),
		text.c_str(),
		text.size(),
		SkTextEncoding::kUTF8,
		SkIntToScalar(pos.x),
		SkIntToScalar(pos.y),
		font,
		paint.skPaint(),
		(SkTextUtils::Align)textAlign
	);
}

constexpr double ease_out_quart(double x) {
	return 1.f - pow(1.f - x, 4.f);
}

constexpr double ease_out_expo(double x) {
	return (x == 1) ? 1 : 1 - std::pow(2, -10 * x);
}

void gui::redraw_window(os::Window* window) {
	os::Surface* s = window->surface();
	const gfx::Rect rc = s->bounds();

	os::Paint text_paint;
	text_paint.color(gfx::rgba(255, 255, 255, 255));

	// background
	os::Paint paint;
	paint.color(gfx::rgba(0, 0, 0, 255));
	s->drawRect(rc, paint);

	// text
	int text_y = 0;

	auto draw_str_temp = [&text_y, &s, &rc](std::string text, gfx::Color colour = gfx::rgba(255, 255, 255, 255)) {
		os::Paint paint;
		paint.color(colour);

		gfx::Point pos(rc.center().x, pad_y + text_y);
		draw_text(s, paint, pos, text, os::TextAlign::Center);

		text_y += font_height + spacing;
	};

	auto draw_wstr_temp = [&draw_str_temp](std::wstring text, gfx::Color colour = gfx::rgba(255, 255, 255, 255)) {
		draw_str_temp(base::to_utf8(text), colour);
	};

	gfx::Rect drop_zone = rc;

	int fill_shade = windowData.dragging ? 30 : 10;
	paint.color(gfx::rgba(255, 255, 255, fill_shade));
	s->drawRect(drop_zone, paint);

	// paint.style(os::Paint::Style::Stroke);
	// const int stroke_shade = 100;
	// const int stroke_width = 2;
	// paint.color(gfx::rgba(255, 255, 255, stroke_shade));
	// paint.strokeWidth(stroke_width);
	// s->drawRect(drop_zone, paint);

	const int blur_stroke_width = 1;
	const float blur_start_shade = 50;

	paint.style(os::Paint::Style::Stroke);
	paint.strokeWidth(blur_stroke_width);

	gfx::Rect blur_drop_zone = drop_zone;
	const int blur_steps = (std::min(rc.w, rc.h) / 2.f / blur_stroke_width);

	for (int i = 0; i < blur_steps; i++) {
		blur_drop_zone.shrink(blur_stroke_width);
		int shade = std::lerp(blur_start_shade, 0, ease_out_quart(i / (float)blur_steps));
		if (shade <= 0)
			break;

		paint.color(gfx::rgba(255, 255, 255, shade));
		s->drawRect(blur_drop_zone, paint);
	}

	draw_text(s, text_paint, drop_zone.center(), "drop a file...", os::TextAlign::Center);

	if (!rendering.queue.empty()) {
		for (const auto [i, render] : helpers::enumerate(rendering.queue)) {
			bool current = render == rendering.current_render;

			draw_wstr_temp(render->get_video_name(), gfx::rgba(255, 255, 255, (current ? 255 : 100)));

			if (current) {
				auto render_status = render->get_status();

				if (render_status.init) {
					draw_str_temp(render_status.progress_string);
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

	font = gui_helpers::create_font_from_data(ttf_FiraCode_Regular, ttf_FiraCode_Regular_len, font_height);

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
