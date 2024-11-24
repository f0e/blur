#include "gui.h"
#include "base/system_console.h"
#include "base/time.h"
#include "gfx/color.h"
#include "gui/ui/render.h"
#include "os/draw_text.h"
#include "os/event.h"
#include "os/event_queue.h"
#include "tasks.h"

#include "ui/ui.h"
#include "ui/keys.h"
#include "ui/utils.h"

#include "resources/fonts.h"

#define DEBUG_RENDER 1

const int font_height = 14;
const int pad_x = 24;
const int pad_y = 35;

const float fps_smoothing = 0.95f;

const float vsync_extra_fps = 50;
const float min_delta_time = 1.f / 10;
const float default_delta_time = 1.f / 60;
float vsync_frame_time = default_delta_time;

bool closing = false;

SkFont font;
SkFont header_font;
SkFont smaller_header_font;
os::EventQueue* event_queue;

void gui::DragTarget::dragEnter(os::DragEvent& ev) {
	// v.dropResult(os::DropOperation::None); // TODO: what does this do? is it needed?

	windowData.dragPosition = ev.position();
	windowData.dragging = true;
}

void gui::DragTarget::dragLeave(os::DragEvent& ev) {
	// todo: not triggering on windows?
	windowData.dragPosition = ev.position();
	windowData.dragging = false;
}

void gui::DragTarget::drag(os::DragEvent& ev) {
	windowData.dragPosition = ev.position();
}

void gui::DragTarget::drop(os::DragEvent& ev) {
	ev.acceptDrop(true);

	if (ev.dataProvider()->contains(os::DragDataItemType::Paths)) {
		std::vector<std::string> paths = ev.dataProvider()->getPaths();
		tasks::add_files(paths);
	}

	windowData.dragging = false;
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

	return window;
}

bool processEvent(const os::Event& ev) {
	switch (ev.type()) {
		case os::Event::CloseApp:
		case os::Event::CloseWindow: {
			closing = true;
			return false;
		}

		case os::Event::ResizeWindow:
			break;

		case os::Event::MouseMove: {
			keys::on_mouse_move(
				ev.position(),
				ev.modifiers(),
				ev.pointerType(),
				ev.pressure()
			);
			return true;
		}

		case os::Event::MouseDown: {
			keys::on_mouse_down(
				ev.position(),
				ev.button(),
				ev.modifiers(),
				ev.pointerType(),
				ev.pressure()
			);
			return true;
		}

		case os::Event::MouseUp: {
			keys::on_mouse_up(
				ev.position(),
				ev.button(),
				ev.modifiers(),
				ev.pointerType()
			);
			return true;
		}

		default:
			break;
	}

	return false;
}

bool gui::generate_messages_from_os_events(bool rendered_last) { // https://github.com/aseprite/aseprite/blob/45c2a5950445c884f5d732edc02989c3fb6ab1a6/src/ui/manager.cpp#L393
	bool processed_an_event = false;

	double timeout;
	if (rendered_last) // an animation is playing or something, so be quick
		timeout = 0.0;
	else // nothing's happening so take your time - but still be fast enough for the ui to update occasionally to see if it wants to render
		timeout = 1.f / 60;

	while (true) {
		os::Event event;

		event_queue->getEvent(event, timeout);

		if (event.type() == os::Event::None)
			break;

		if (processEvent(event))
			processed_an_event = true;

		timeout = 0.0; // i think this is correct, allow blocking for first event but any subsequent one should be instant
	}

	return processed_an_event;
}

void gui::event_loop() {
	bool to_render = true;

	while (!closing) {
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
			double rate = utils::get_display_refresh_rate(screen_handle);
			vsync_frame_time = 1.f / (rate + vsync_extra_fps);
			printf("switched screen, updated vsync_frame_time. refresh rate: %.2f hz\n", rate);
			last_screen_handle = screen_handle;
		}

		auto frame_start = std::chrono::steady_clock::now();

		bool rendered = redraw_window(window.get(), to_render); // note: rendered isn't true if rendering was forced, it's only if an animation or smth is playing
		to_render = generate_messages_from_os_events(rendered); // true if input handled

#if DEBUG_RENDER
		printf("rendered: %d, to render: %d\n", rendered, to_render);
#endif

		if (rendered || to_render) {
			auto target_time = frame_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
												 std::chrono::duration<float>(vsync_frame_time)
											 );
			std::this_thread::sleep_until(target_time);
		}
	}
}

bool gui::redraw_window(os::Window* window, bool force_render) {
	auto now = std::chrono::steady_clock::now();
	static auto last_frame_time = now;

	// todo: first render in a batch might be fucked, look at progress bar skipping fully to complete instantly on 25 speed - investigate
	static bool first = true;
	float delta_time;

#if DEBUG_RENDER
	float fps = -1.f;
#endif

	if (first) {
		delta_time = default_delta_time;
		first = false;
	}
	else {
		float time_since_last_frame = std::chrono::duration<float>(std::chrono::steady_clock::now() - last_frame_time).count();

#if DEBUG_RENDER
		fps = 1.f / time_since_last_frame;

// float current_fps = 1.f / time_since_last_frame;
// if (fps == -1.f)
// 	fps = current_fps;
// fps = (fps * fps_smoothing) + (current_fps * (1.0f - fps_smoothing));
#endif

		delta_time = std::min(time_since_last_frame, min_delta_time);
	}

	last_frame_time = now;

	os::Surface* s = window->surface();
	const gfx::Rect rc = s->bounds();

	gfx::Rect drop_zone = rc;

	static float bg_overlay_shade = 0.f;
	float last_fill_shade = bg_overlay_shade;
	bg_overlay_shade = std::lerp(bg_overlay_shade, windowData.dragging ? 30.f : 0.f, 25.f * delta_time);
	force_render |= bg_overlay_shade != last_fill_shade;

	{
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
	}

	static ui::Container container;

	gfx::Rect container_rect = rc;
	container_rect.x += pad_x;
	container_rect.w -= pad_x * 2;
	container_rect.y += pad_y;
	container_rect.h -= pad_y * 2;

	ui::init_container(container, container_rect, font);

	static float bar_percent = 0.f;

	if (rendering.queue.empty()) {
		bar_percent = 0.f;

		gfx::Point title_pos = rc.center();
		title_pos.y = pad_y + header_font.getSize();

		ui::add_text_fixed("blur title text", container, title_pos, "blur", gfx::rgba(255, 255, 255, 255), header_font, os::TextAlign::Center, 15);
		ui::add_button("open a file button", container, "Open a file", font, [] {
			base::paths paths;
			utils::show_file_selector("Blur input", ".", { "mp4", "mkv" }, os::FileDialog::Type::OpenFiles, paths);
			tasks::add_files(paths);
		});
		ui::add_text("drop a file text", container, "or drop a file anywhere", gfx::rgba(255, 255, 255, 255), font, os::TextAlign::Center);
	}
	else {
		bool is_progress_shown = false;

		for (const auto [i, render] : helpers::enumerate(rendering.queue)) {
			bool current = render == rendering.current_render;

			// todo: ui concept
			// screen start|      [faded]last_video current_video [faded]next_video next_video2 next_video3 (+5) | screen end
			// animate sliding in as it moves along the queue
			ui::add_text(fmt::format("video name text {}", i), container, base::to_utf8(render->get_video_name()), gfx::rgba(255, 255, 255, (current ? 255 : 100)), smaller_header_font, os::TextAlign::Center, current ? 15 : 7);

			if (current) {
				auto render_status = render->get_status();
				int bar_width = 300;

				std::string preview_path = render->get_preview_path().string();
				if (!preview_path.empty() && render_status.current_frame > 0) {
					auto element = ui::add_image("preview image", container, preview_path, gfx::Size(container_rect.w, 200), std::to_string(render_status.current_frame));
					if (element) {
						bar_width = (*element)->rect.w;
					}
				}

				if (render_status.init) {
					float render_progress = render_status.current_frame / (float)render_status.total_frames;
					bar_percent = std::lerp(bar_percent, render_progress, 5.f * delta_time);

					ui::add_bar("progress bar", container, bar_percent, gfx::rgba(51, 51, 51, 255), gfx::rgba(255, 255, 255, 255), bar_width, fmt::format("{:.1f}%", render_progress * 100), gfx::rgba(255, 255, 255, 255), &font);
					ui::add_text("progress text", container, fmt::format("frame {}/{}", render_status.current_frame, render_status.total_frames), gfx::rgba(255, 255, 255, 155), font, os::TextAlign::Center);
					ui::add_text("progress text 2", container, fmt::format("{:.2f} frames per second", render_status.fps), gfx::rgba(255, 255, 255, 155), font, os::TextAlign::Center);

					is_progress_shown = true;
				}
				else {
					ui::add_text("initialising render text", container, "Initialising render...", gfx::rgba(255, 255, 255, 255), font, os::TextAlign::Center);
				}
			}
		}

		if (!is_progress_shown) {
			bar_percent = 0.f; // Reset when no progress bar is shown
		}
	}

	ui::center_elements_in_container(container);

	bool want_to_render = ui::update_container(s, container, delta_time);
	if (!want_to_render && !force_render)
		// note: DONT RENDER ANYTHING ABOVE HERE!!! todo: render queue?
		return false;

	// background
	os::Paint paint;
	paint.color(gfx::rgba(0, 0, 0, 255));
	s->drawRect(rc, paint);

	if ((int)bg_overlay_shade > 0) {
		paint.color(gfx::rgba(255, 255, 255, bg_overlay_shade));
		s->drawRect(drop_zone, paint);
	}

#if DEBUG_RENDER
	{
		// debug
		static const int debug_box_size = 30;
		static float x = rc.x2() - debug_box_size, y = 100.f;
		static bool right = false;
		static bool down = true;
		x += 1.f * (right ? 1 : -1);
		y += 1.f * (down ? 1 : -1);
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
#endif

	ui::render_container(s, container);

#if DEBUG_RENDER
	if (fps != -1.f) {
		gfx::Point fps_pos(
			rc.x2() - pad_x,
			rc.y + pad_y
		);
		render::text(s, fps_pos, gfx::rgba(0, 255, 0, 255), fmt::format("{:.0f} fps", fps), font, os::TextAlign::Right);
	}
#endif

	// todo: whats this do
	if (!window->isVisible())
		window->setVisible(true);

	window->invalidateRegion(gfx::Region(rc));

	return want_to_render;
}

void gui::on_resize(os::Window* window) {
	redraw_window(window, true);
}

void gui::run() {
	auto system = os::make_system();

	font = SkFont(); // default font
	header_font = utils::create_font_from_data(EBGaramond_VariableFont_wght_ttf, EBGaramond_VariableFont_wght_ttf_len, 30);
	smaller_header_font = header_font;
	smaller_header_font.setSize(18.f);

	system->setAppMode(os::AppMode::GUI);
	system->handleWindowResize = on_resize;

	DragTarget dragTarget;
	window = create_window(dragTarget);

	system->finishLaunching();
	system->activateApp();

	base::SystemConsole systemConsole;
	systemConsole.prepareShell();

	event_queue = system->eventQueue();

	event_loop();
}
