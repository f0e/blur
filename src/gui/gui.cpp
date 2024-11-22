#include "gui.h"
#include "gfx/color.h"
#include "gfx/rgb.h"
#include "tasks.h"
#include "gui_helpers.h"
#include "easing.h"

#include "resources/font.h"

const int font_height = 14;
const int spacing = 8;
const int pad_x = 24;
const int pad_y = 35;

const int preview_gap = font_height + 50;
const int preview_width = 300;
const int preview_stroke_width = 1;

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

void draw_text(os::Surface* s, const gfx::Color colour, gfx::Point pos, std::string text, os::TextAlign textAlign) {
	// todo: is creating a new paint instance every time significant to perf? shouldnt be
	os::Paint paint;
	paint.color(colour);

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
		static float x = 0;
		static bool right = true;
		x += 500.f * delta_time * (right ? 1 : -1);
		os::Paint paint;
		paint.color(gfx::rgba(255, 0, 0, 50));
		s->drawRect(gfx::Rect(x, 100, 30, 30), paint);

		if (right) {
			if (x > rc.x2())
				right = false;
		}
		else {
			if (x < 0)
				right = true;
		}
	}

	// text
	int text_y = pad_y;

	auto draw_str_temp = [&text_y, &s, &rc](std::string text, gfx::Color colour = gfx::rgba(255, 255, 255, 255)) {
		gfx::Point pos(rc.center().x, text_y);
		draw_text(s, colour, pos, text, os::TextAlign::Center);

		text_y += font_height + spacing;
	};

	auto draw_wstr_temp = [&draw_str_temp](std::wstring text, gfx::Color colour = gfx::rgba(255, 255, 255, 255)) {
		draw_str_temp(base::to_utf8(text), colour);
	};

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

	const int blur_stroke_width = 1;
	const float blur_start_shade = 50;
	const float blur_screen_percent = 0.8f;

	paint.style(os::Paint::Style::Stroke);
	paint.strokeWidth(blur_stroke_width);

	gfx::Rect blur_drop_zone = drop_zone;
	const int blur_steps = (std::min(rc.w, rc.h) / 2.f / blur_stroke_width) * blur_screen_percent;

	for (int i = 0; i < blur_steps; i++) {
		blur_drop_zone.shrink(blur_stroke_width);
		int shade = std::lerp(blur_start_shade, 0, ease_out_quart(i / (float)blur_steps));
		if (shade <= 0)
			break;

		paint.color(gfx::rgba(255, 255, 255, shade));
		s->drawRect(blur_drop_zone, paint);
	}

	static float text_alpha = 1.f;
	static float text_offset = 0.f;
	static const float text_animation_speed = 20.f;
	text_alpha = std::lerp(text_alpha, rendering.queue.empty() ? 1.f : 0.f, text_animation_speed * delta_time);
	text_offset = std::lerp(text_offset, rendering.queue.empty() ? 0.f : -10.f, text_animation_speed * delta_time);
	draw_text(s, gfx::rgba(255, 255, 255, text_alpha * 255), drop_zone.center() + gfx::Point(0, text_offset), "drop a file...", os::TextAlign::Center);

	if (!rendering.queue.empty()) {
		for (const auto [i, render] : helpers::enumerate(rendering.queue)) {
			bool current = render == rendering.current_render;

			draw_wstr_temp(render->get_video_name(), gfx::rgba(255, 255, 255, (current ? 255 : 100)));

			if (current) {
				auto render_status = render->get_status();

				os::SurfaceRef img_surface = os::instance()->loadRgbaSurface(render->get_preview_path().c_str());
				if (img_surface) {
					gfx::Rect preview_rect = rc;
					preview_rect.y += pad_y + preview_gap;
					preview_rect.h = rc.h - preview_rect.y - pad_y;
					preview_rect.w = preview_rect.h * (img_surface->width() / (float)img_surface->height());

					// limit width
					int max_w = rc.w - pad_x * 2;
					if (preview_rect.w > max_w) {
						preview_rect.w = max_w;
						preview_rect.h = preview_rect.w * (img_surface->height() / (float)img_surface->width());
					}

					preview_rect.x = rc.center().x - preview_rect.w / 2;

					s->drawSurface(img_surface.get(), img_surface->bounds(), preview_rect);

					if (preview_stroke_width > 0) {
						os::Paint stroke_paint;
						stroke_paint.style(os::Paint::Style::Stroke);
						stroke_paint.color(gfx::rgba(255, 255, 255, 200));
						stroke_paint.strokeWidth(preview_stroke_width);
						s->drawRect(preview_rect.enlarge(1), stroke_paint);
					}
				}

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

	event_queue = system->eventQueue();

	event_loop();
}
