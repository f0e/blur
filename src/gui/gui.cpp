#include "gui.h"
#include "tasks.h"
#include "font.h"

#include "os/skia/skia_helpers.h"
#include "os/skia/skia_surface.h"
#include "include/core/SkTextBlob.h"
#include "include/utils/SkTextUtils.h"
#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkData.h"

const int font_height = 18;
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

	os::WindowRef window = os::instance()->makeWindow(500, 350);
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

	// background
	int bg_shade = windowData.dragging ? 10 : 0;
	paint.color(gfx::rgba(bg_shade, bg_shade, bg_shade, 255));
	s->drawRect(rc, paint);

	// text
	int y = 0;

	auto draw_str_temp = [&y, &s, &paint](std::string text, gfx::Color colour = gfx::rgba(255, 255, 255, 255)) {
		paint.color(colour);

		os::TextAlign textAlign = os::TextAlign::Left;

		gfx::Point pos(pad_x, pad_y + y);

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

		y += font_height + spacing;
	};

	auto draw_wstr_temp = [&draw_str_temp](std::wstring text, gfx::Color colour = gfx::rgba(255, 255, 255, 255)) {
		draw_str_temp(base::to_utf8(text), colour);
	};

	draw_str_temp("blur");
	draw_str_temp("drop a file...");

	if (!rendering.queue.empty()) {
		for (const auto [i, render] : helpers::enumerate(rendering.queue)) {
			bool current = render == rendering.current_render;

			draw_wstr_temp(render->get_video_name(), gfx::rgba(255, 255, 255, (current ? 255 : 100)));

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

SkFont createFontFromTrueType(const unsigned char* fontData, size_t dataSize, float fontHeight) {
	sk_sp<SkData> skData = SkData::MakeWithCopy(fontData, dataSize);

	// Create a typeface from SkData
	sk_sp<SkTypeface> typeface = SkTypeface::MakeFromData(skData);

	if (!typeface) {
		printf("failed to create font\n");
		return SkFont();
	}

	return SkFont(typeface, SkIntToScalar(fontHeight));
}

void gui::run() {
	auto system = os::make_system();

	font = createFontFromTrueType(VT323_Regular_ttf, VT323_Regular_ttf_len, font_height);

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
