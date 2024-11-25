
#include "renderer.h"

#include "drag_handler.h"
#include "tasks.h"
#include "gui.h"

#include "ui/ui.h"
#include "ui/utils.h"
#include "ui/render.h"

#include "resources/fonts.h"

#define DEBUG_RENDER         1
#define DEBUG_RENDER_LOGGING 0

const int font_height = 14;
const int pad_x = 24;
const int pad_y = 35;

const float fps_smoothing = 0.95f;

void gui::renderer::init_fonts() {
	fonts::font = SkFont(); // default font
	fonts::header_font = utils::create_font_from_data(
		EBGaramond_VariableFont_wght_ttf,
		EBGaramond_VariableFont_wght_ttf_len,
		30
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

bool gui::renderer::redraw_window(os::Window* window, bool force_render) {
	set_cursor_this_frame = false;

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
	bg_overlay_shade = std::lerp(bg_overlay_shade, drag_handler::dragging ? 30.f : 0.f, 25.f * delta_time);
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

	ui::init_container(container, container_rect, fonts::font);

	static float bar_percent = 0.f;

	if (rendering.queue.empty()) {
		bar_percent = 0.f;

		gfx::Point title_pos = rc.center();
		title_pos.y = pad_y + fonts::header_font.getSize();

		ui::add_text_fixed("blur title text", container, title_pos, "blur", gfx::rgba(255, 255, 255, 255), fonts::header_font, os::TextAlign::Center, 15);
		ui::add_button("open file button", container, "Open files", fonts::font, [] {
			base::paths paths;
			utils::show_file_selector("Blur input", "", {}, os::FileDialog::Type::OpenFiles, paths);
			tasks::add_files(paths);
		});
		ui::add_text("drop file text", container, "or drop them anywhere", gfx::rgba(255, 255, 255, 255), fonts::font, os::TextAlign::Center);
	}
	else {
		bool is_progress_shown = false;

		for (const auto [i, render] : helpers::enumerate(rendering.queue)) {
			bool current = render == rendering.current_render;

			// todo: ui concept
			// screen start|      [faded]last_video current_video [faded]next_video next_video2 next_video3 (+5) | screen end
			// animate sliding in as it moves along the queue
			ui::add_text(fmt::format("video name text {}", i), container, base::to_utf8(render->get_video_name()), gfx::rgba(255, 255, 255, (current ? 255 : 100)), fonts::smaller_header_font, os::TextAlign::Center, current ? 15 : 7);

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

					ui::add_bar("progress bar", container, bar_percent, gfx::rgba(51, 51, 51, 255), gfx::rgba(255, 255, 255, 255), bar_width, fmt::format("{:.1f}%", render_progress * 100), gfx::rgba(255, 255, 255, 255), &fonts::font);
					ui::add_text("progress text", container, fmt::format("frame {}/{}", render_status.current_frame, render_status.total_frames), gfx::rgba(255, 255, 255, 155), fonts::font, os::TextAlign::Center);
					ui::add_text("progress text 2", container, fmt::format("{:.2f} frames per second", render_status.fps), gfx::rgba(255, 255, 255, 155), fonts::font, os::TextAlign::Center);

					is_progress_shown = true;
				}
				else {
					ui::add_text("initialising render text", container, "Initialising render...", gfx::rgba(255, 255, 255, 255), fonts::font, os::TextAlign::Center);
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
		render::text(s, fps_pos, gfx::rgba(0, 255, 0, 255), fmt::format("{:.0f} fps", fps), fonts::font, os::TextAlign::Right);
	}
#endif

	// todo: whats this do
	if (!window->isVisible())
		window->setVisible(true);

	window->invalidateRegion(gfx::Region(rc));

	// reset cursor
	if (!set_cursor_this_frame) {
		set_cursor(os::NativeCursor::Arrow);
	}

	return want_to_render;
}
