#include "renderer.h"

#include "common/config_app.h"
#include "common/rendering.h"

#include "gui/ui/keys.h"
#include "sdl.h"
#include "gui.h"

#include "ui/ui.h"
#include "render/render.h"
#include "os/desktop_notification.h"

#include "components/main.h"
#include "components/notifications.h"
#include "components/configs/configs.h"

#define DEBUG_RENDER 0

bool gui::renderer::redraw_window(bool rendered_last, bool want_to_render) {
	keys::on_frame_start();
	ui::on_frame_start();
	sdl::on_frame_start();

	render::update_window_size(sdl::window);

	auto now = std::chrono::steady_clock::now();
	static auto last_frame_time = now;

#if DEBUG_RENDER
	float fps = -1.f;
#endif
	float delta_time =
		sdl::vsync_frame_time_ms / 1000.f; // TODO: TEMP: using vsync fps as deltatime to avoid jumping issues. let's
	                                       // see if it causes any issues. it should be fine...

	if (!rendered_last) {
		// delta_time = sdl::DEFAULT_DELTA_TIME;
	}
	else {
		float time_since_last_frame = std::chrono::duration<float>(now - last_frame_time).count();

#if DEBUG_RENDER
		fps = 1.f / time_since_last_frame;

// float current_fps = 1.f / time_since_last_frame;
// if (fps == -1.f)
// 	fps = current_fps;
// fps = (fps * FPS_SMOOTHING) + (current_fps * (1.0f - FPS_SMOOTHING));
#endif

		// delta_time = std::min(time_since_last_frame, 1.f / sdl::MIN_FPS);
	}

	// #if DEBUG_RENDER
	// 	u::log("delta time: {}, rendered last: {}", delta_time, rendered_last);
	// #endif

	last_frame_time = now;

	const gfx::Rect rect(gfx::Point(0, 0), render::window_size);

	static float bg_drop_overlay_percent = 0.f;
	static float bg_last_percent = bg_drop_overlay_percent;
	bg_drop_overlay_percent = u::lerp(bg_drop_overlay_percent, gui::dragging ? 1.f : 0.f, 25.f * delta_time);
	want_to_render |= bg_drop_overlay_percent != bg_last_percent;
	bg_last_percent = bg_drop_overlay_percent;

	gfx::Rect nav_container_rect = rect;
	nav_container_rect.h = 70;
	nav_container_rect.y = rect.y2() - nav_container_rect.h;

	ui::reset_container(nav_container, sdl::window, nav_container_rect, fonts::dejavu.height(), {});

	int nav_cutoff = rect.y2() - nav_container_rect.y;
	int bottom_pad = std::max(PAD_Y, nav_cutoff);

	const static int main_pad_x = std::min(100, render::window_size.w / 10); // bit of magic never hurt anyone
	ui::reset_container(
		main_container, sdl::window, rect, 13, ui::Padding{ PAD_Y, main_pad_x, bottom_pad, main_pad_x }
	);

	const int config_page_container_gap = PAD_X / 2;

	gfx::Rect config_container_rect = rect;

	if (components::configs::loaded_config) {
		config_container_rect.w = 200 + PAD_X * 2;
	}

	ui::reset_container(
		config_container, sdl::window, config_container_rect, 9, ui::Padding{ PAD_Y, PAD_X, bottom_pad, PAD_X }
	);

	gfx::Rect config_preview_container_rect = rect;
	config_preview_container_rect.x = config_container_rect.x2() + config_page_container_gap;
	config_preview_container_rect.w -= config_container_rect.w + config_page_container_gap;

	gfx::Rect config_preview_header_container_rect = config_preview_container_rect;
	config_preview_header_container_rect.h = 80;

	ui::reset_container(
		config_preview_header_container,
		sdl::window,
		config_preview_header_container_rect,
		fonts::dejavu.height(),
		ui::Padding{ PAD_Y, PAD_X }
	);

	gfx::Rect config_preview_content_container_rect = config_preview_container_rect;
	config_preview_content_container_rect.y = config_preview_header_container_rect.y2();
	config_preview_content_container_rect.h -= config_preview_header_container_rect.h;

	ui::reset_container(
		config_preview_content_container,
		sdl::window,
		config_preview_content_container_rect,
		fonts::dejavu.height(),
		ui::Padding{ 0, PAD_X, bottom_pad, PAD_X }
	);

	ui::reset_container(
		option_information_container,
		sdl::window,
		config_preview_container_rect,
		9,
		ui::Padding{ PAD_Y, PAD_X, bottom_pad, PAD_X }
	);

	gfx::Rect notification_container_rect = rect;
	notification_container_rect.w = 230;
	notification_container_rect.x =
		rect.x2() - notification_container_rect.w - components::notifications::NOTIFICATIONS_PAD_X;
	notification_container_rect.h = 300;
	notification_container_rect.y = components::notifications::NOTIFICATIONS_PAD_Y;

	ui::reset_container(notification_container, sdl::window, notification_container_rect, 6, {});

	switch (screen) {
		case Screens::MAIN: {
			components::configs::loaded_config = false;

			components::main::home_screen(main_container, delta_time);

			if (initialisation_res) {
				auto current_render = rendering.get_current_render();
				if (current_render) {
					ui::add_button(
						(*current_render)->is_paused() ? "resume render button" : "pause render button",
						nav_container,
						(*current_render)->is_paused() ? "Resume" : "Pause",
						fonts::dejavu,
						[] {
							auto current_render = rendering.get_current_render();
							if ((*current_render)->is_paused())
								(*current_render)->resume();
							else
								(*current_render)->pause();
						}
					);

					ui::set_next_same_line(nav_container);
					ui::add_button("stop render button", nav_container, "Cancel", fonts::dejavu, [] {
						auto current_render = rendering.get_current_render();
						if (current_render)
							(*current_render)->stop();
					});

					ui::set_next_same_line(nav_container);
					components::main::open_files_button(nav_container, "Add files");
				}

				ui::set_next_same_line(nav_container);
				ui::add_button("config button", nav_container, "Config", fonts::dejavu, [] {
					screen = Screens::CONFIG;
				});
			}

			ui::center_elements_in_container(main_container);

			break;
		}
		case Screens::CONFIG: {
			ui::set_next_same_line(nav_container);
			ui::add_button("back button", nav_container, "Back", fonts::dejavu, [] {
				screen = Screens::MAIN;
			});

			components::configs::screen(
				config_container,
				nav_container,
				config_preview_header_container,
				config_preview_content_container,
				option_information_container,
				delta_time
			);

			ui::center_elements_in_container(config_preview_header_container);
			ui::center_elements_in_container(config_preview_content_container);
			ui::center_elements_in_container(option_information_container, true, false);

			break;
		}
	}

	components::notifications::render(notification_container);

	ui::center_elements_in_container(nav_container);

	want_to_render |= ui::update_container_frame(notification_container, delta_time);
	want_to_render |= ui::update_container_frame(nav_container, delta_time);

	want_to_render |= ui::update_container_frame(main_container, delta_time);
	want_to_render |= ui::update_container_frame(config_container, delta_time);
	want_to_render |= ui::update_container_frame(config_preview_header_container, delta_time);
	want_to_render |= ui::update_container_frame(config_preview_content_container, delta_time);
	want_to_render |= ui::update_container_frame(option_information_container, delta_time);
	ui::on_update_frame_end();

	if (!want_to_render)
		// note: DONT RENDER ANYTHING ABOVE HERE!!! todo: render queue?
		return false;

	render::imgui.begin(sdl::window);
	{
		// background
		render::rect_filled(rect, gfx::Color(0, 0, 0, 255));

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

			render::rect_filled(gfx::Rect(x, y, debug_box_size, debug_box_size), gfx::Color(255, 0, 0, 50));

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

		// front -> back
		ui::render_container(main_container);
		ui::render_container(config_container);
		ui::render_container(config_preview_content_container);
		ui::render_container(config_preview_header_container);
		ui::render_container(option_information_container);
		ui::render_container(nav_container);
		ui::render_container(notification_container);

		// file drop overlay
		if (bg_drop_overlay_percent > 0.f)
			render::rect_filled(rect, gfx::Color::white(bg_drop_overlay_percent * 30.f));

#if DEBUG_RENDER
		if (fps != -1.f) {
			gfx::Point fps_pos(rect.x2() - PAD_X, rect.y + PAD_Y);
			render::text(
				fps_pos, gfx::Color(0, 255, 0, 255), std::format("{:.0f} fps", fps), fonts::dejavu, FONT_RIGHT_ALIGN
			);
		}
#endif
	}
	render::imgui.end(sdl::window);

	ui::on_frame_end();

	return true;
}

void gui::renderer::on_render_finished(Render* render, const tl::expected<RenderResult, std::string>& result) {
	if (!result) {
		gui::components::notifications::add(
			std::format("Render '{}' failed. Click to copy error message", render->get_video_name()),
			ui::NotificationType::NOTIF_ERROR,
			[result](const std::string& id) {
				SDL_SetClipboardText(result.error().c_str());

				gui::components::notifications::close(id);

				gui::components::notifications::add(
					"Copied error message to clipboard",
					ui::NotificationType::INFO,
					{},
					std::chrono::duration<float>(2.f)
				);
			},
			std::nullopt
		);

		auto app_config = config_app::get_app_config();
		if (app_config.render_failure_notifications) {
			desktop_notification::show("Blur render failed", u::truncate_with_ellipsis(result.error(), 100));
		}
		return;
	}

	if (result->stopped) {
		gui::components::notifications::add(
			std::format("Render '{}' stopped", render->get_video_name()), ui::NotificationType::INFO
		);
		return;
	}

	auto output_path = render->get_output_video_path();

	gui::components::notifications::add(
		std::format("Render '{}' completed", render->get_video_name()),
		ui::NotificationType::SUCCESS,
		[output_path](const std::string& id) {
			// Convert path to a file:// URL for SDL_OpenURL
			std::string file_url = "file://" + output_path.string();
			if (!SDL_OpenURL(file_url.c_str())) {
				u::log_error("Failed to open output folder: {}", SDL_GetError());
			}
		}
	);

	auto app_config = config_app::get_app_config();
	if (app_config.render_success_notifications) {
		desktop_notification::show("Blur render complete", "Render completed successfully");
	}
}
