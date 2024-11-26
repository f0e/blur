
#include "renderer.h"

#include <common/rendering.h>

#include "drag_handler.h"
#include "tasks.h"
#include "gui.h"

#include "ui/ui.h"
#include "ui/utils.h"
#include "ui/render.h"

#include "resources/fonts.h"

#define DEBUG_RENDER         1
#define DEBUG_RENDER_LOGGING 0

const int PAD_X = 24;
const int PAD_Y = 35;

const float FPS_SMOOTHING = 0.95f;

void gui::renderer::init_fonts() {
	fonts::font = SkFont(); // default font
	fonts::header_font = utils::create_font_from_data(
		EBGaramond_VariableFont_wght_ttf.data(), EBGaramond_VariableFont_wght_ttf.size(), 30
	);
	fonts::smaller_header_font = fonts::header_font;
	fonts::smaller_header_font.setSize(18.f);
}

void gui::renderer::set_cursor(os::NativeCursor cursor) {
	if (current_cursor != cursor) {
		current_cursor = cursor;
		window->setCursor(cursor);
	}

	set_cursor_this_frame = true;
}

// NOLINTBEGIN(readability-function-size,readability-function-cognitive-complexity)
bool gui::renderer::redraw_window(os::Window* window, bool force_render) {
	set_cursor_this_frame = false;

	auto now = std::chrono::steady_clock::now();
	static auto last_frame_time = now;

	// todo: first render in a batch might be fucked, look at progress bar skipping fully to complete instantly on 25
	// speed - investigate
	static bool first = true;

#if DEBUG_RENDER
	float fps = -1.f;
#endif
	float delta_time = NAN;

	if (first) {
		delta_time = DEFAULT_DELTA_TIME;
		first = false;
	}
	else {
		float time_since_last_frame =
			std::chrono::duration<float>(std::chrono::steady_clock::now() - last_frame_time).count();

#if DEBUG_RENDER
		fps = 1.f / time_since_last_frame;

// float current_fps = 1.f / time_since_last_frame;
// if (fps == -1.f)
// 	fps = current_fps;
// fps = (fps * FPS_SMOOTHING) + (current_fps * (1.0f - FPS_SMOOTHING));
#endif

		delta_time = std::min(time_since_last_frame, MIN_DELTA_TIME);
	}

	last_frame_time = now;

	os::Surface* surface = window->surface();
	const gfx::Rect rect = surface->bounds();

	static float bg_overlay_shade = 0.f;
	float last_fill_shade = bg_overlay_shade;
	bg_overlay_shade = std::lerp(bg_overlay_shade, drag_handler::dragging ? 30.f : 0.f, 25.f * delta_time);
	force_render |= bg_overlay_shade != last_fill_shade;

	static ui::Container container;

	gfx::Rect container_rect = rect;
	container_rect.x += PAD_X;
	container_rect.w -= PAD_X * 2;
	container_rect.y += PAD_Y;
	container_rect.h -= PAD_Y * 2;

	ui::reset_container(container, container_rect, fonts::font);

	static float bar_percent = 0.f;

	if (rendering.get_queue().empty()) {
		bar_percent = 0.f;

		gfx::Point title_pos = rect.center();
		title_pos.y = int(PAD_Y + fonts::header_font.getSize());

		ui::add_text_fixed(
			"blur title text",
			container,
			title_pos,
			"blur",
			gfx::rgba(255, 255, 255, 255),
			fonts::header_font,
			os::TextAlign::Center
		);
		ui::add_button("open file button", container, "Open files", fonts::font, [] {
			base::paths paths;
			utils::show_file_selector("Blur input", "", {}, os::FileDialog::Type::OpenFiles, paths);
			tasks::add_files(paths);
		});
		ui::add_text(
			"drop file text",
			container,
			"or drop them anywhere",
			gfx::rgba(255, 255, 255, 255),
			fonts::font,
			os::TextAlign::Center
		);
	}
	else {
		bool is_progress_shown = false;

		rendering.lock();
		{
			for (const auto [i, render] : u::enumerate(rendering.get_queue())) {
				bool current = render.get() == rendering.get_current_render();

				// todo: ui concept
				// screen start|      [faded]last_video current_video [faded]next_video next_video2 next_video3 (+5) |
				// screen end animate sliding in as it moves along the queue
				ui::add_text(
					std::format("video name text {}", i),
					container,
					base::to_utf8(render->get_video_name()),
					gfx::rgba(255, 255, 255, (current ? 255 : 100)),
					fonts::smaller_header_font,
					os::TextAlign::Center,
					current ? 15 : 7
				);

				if (current) {
					auto render_status = render->get_status();
					int bar_width = 300;

					std::string preview_path = render->get_preview_path().string();
					if (!preview_path.empty() && render_status.current_frame > 0) {
						auto element = ui::add_image(
							"preview image",
							container,
							preview_path,
							gfx::Size(container_rect.w, 200),
							std::to_string(render_status.current_frame)
						);
						if (element) {
							bar_width = (*element)->rect.w;
						}
					}

					if (render_status.init) {
						float render_progress = (float)render_status.current_frame / (float)render_status.total_frames;
						bar_percent = std::lerp(bar_percent, render_progress, 5.f * delta_time);

						ui::add_bar(
							"progress bar",
							container,
							bar_percent,
							gfx::rgba(51, 51, 51, 255),
							gfx::rgba(255, 255, 255, 255),
							bar_width,
							std::format("{:.1f}%", render_progress * 100),
							gfx::rgba(255, 255, 255, 255),
							&fonts::font
						);
						ui::add_text(
							"progress text",
							container,
							std::format("frame {}/{}", render_status.current_frame, render_status.total_frames),
							gfx::rgba(255, 255, 255, 155),
							fonts::font,
							os::TextAlign::Center
						);
						ui::add_text(
							"progress text 2",
							container,
							std::format("{:.2f} frames per second", render_status.fps),
							gfx::rgba(255, 255, 255, 155),
							fonts::font,
							os::TextAlign::Center
						);

						is_progress_shown = true;
					}
					else {
						ui::add_text(
							"initialising render text",
							container,
							"Initialising render...",
							gfx::rgba(255, 255, 255, 255),
							fonts::font,
							os::TextAlign::Center
						);
					}
				}
			}
		}
		rendering.unlock();

		if (!is_progress_shown) {
			bar_percent = 0.f; // Reset when no progress bar is shown
		}
	}

	ui::center_elements_in_container(container);

	bool want_to_render = ui::update_container(container, delta_time);
	if (!want_to_render && !force_render)
		// note: DONT RENDER ANYTHING ABOVE HERE!!! todo: render queue?
		return false;

	// background
	render::rect_filled(surface, rect, gfx::rgba(0, 0, 0, 255));

	if ((int)bg_overlay_shade > 0)
		render::rect_filled(surface, rect, gfx::rgba(255, 255, 255, (gfx::ColorComponent)bg_overlay_shade));

#if DEBUG_RENDER
	{
		// debug
		static const int debug_box_size = 30;
		static float x = rect.x2() - debug_box_size;
		static float y = 100.f;
		static bool right = false;
		static bool down = true;
		x += 1.f * (right ? 1 : -1);
		y += 1.f * (down ? 1 : -1);

		render::rect_filled(surface, gfx::Rect(x, y, debug_box_size, debug_box_size), gfx::rgba(255, 0, 0, 50));

		if (right) {
			if (x + debug_box_size > rect.x2())
				right = false;
		}
		else {
			if (x < 0)
				right = true;
		}

		if (down) {
			if (y + debug_box_size > rect.y2())
				down = false;
		}
		else {
			if (y < 0)
				down = true;
		}
	}
#endif

	ui::render_container(surface, container);

#if DEBUG_RENDER
	if (fps != -1.f) {
		gfx::Point fps_pos(rect.x2() - PAD_X, rect.y + PAD_Y);
		render::text(
			surface,
			fps_pos,
			gfx::rgba(0, 255, 0, 255),
			std::format("{:.0f} fps", fps),
			fonts::font,
			os::TextAlign::Right
		);
	}
#endif

	// todo: whats this do
	if (!window->isVisible())
		window->setVisible(true);

	window->invalidateRegion(gfx::Region(rect));

	// reset cursor
	if (!set_cursor_this_frame) {
		set_cursor(os::NativeCursor::Arrow);
	}

	return want_to_render;
}

// NOLINTEND(readability-function-size,readability-function-cognitive-complexity)
