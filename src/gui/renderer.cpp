#include "renderer.h"

#include "common/config_app.h"
#include "common/rendering.h"

#include "gui/ui/keys.h"
#include "sdl.h"
#include "gui.h"

#include "ui/ui.h"
#include "render/render.h"
#include "fonts/icons.h"
#include "os/desktop_notification.h"

#include "components/main.h"
#include "components/notifications.h"
#include "components/update_notice.h"
#include "components/test.h"
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

	const int navigation_button_size = ui::button_height(fonts::dejavu);
	gfx::Rect navigation_button_container_rect;

	if (screen == Screens::CONFIG) {
		navigation_button_container_rect =
			gfx::Rect(rect.x + PAD_X, rect.y + PAD_Y, navigation_button_size, navigation_button_size);
	}
	else {
		navigation_button_container_rect = gfx::Rect(
			rect.x + PAD_X,
			nav_container_rect.y + ((nav_container_rect.h - navigation_button_size) / 2),
			navigation_button_size,
			navigation_button_size
		);
	}
	ui::reset_container(navigation_button_container, sdl::window, navigation_button_container_rect, 0, {});

	int nav_cutoff = rect.y2() - nav_container_rect.y;
	int bottom_pad = std::max(PAD_Y, nav_cutoff);

	const static int main_pad_x = std::min(100, render::window_size.w / 10); // bit of magic never hurt anyone
	ui::reset_container(
		main_container, sdl::window, rect, 13, ui::Padding{ PAD_Y, main_pad_x, bottom_pad, main_pad_x }
	);

	const int config_page_container_gap = PAD_X / 2;

	const int config_container_element_gap = 9;

	gfx::Rect config_container_rect = rect;

	const int base_config_width = 200 + PAD_X * 2;

	// presets need the room, ffmpeg commands are long
	int config_goal_width = components::configs::selected_config_tab == "presets" ? rect.w : base_config_width;

	static float config_width = config_goal_width;
	float last_config_width = config_width;
	config_width = u::lerp(config_width, (float)config_goal_width, 25.f * delta_time, 0.5f);
	want_to_render |= config_width != last_config_width;

	config_container_rect.w = std::lround(config_width);

	ui::reset_container(
		config_container,
		sdl::window,
		config_container_rect,
		config_container_element_gap,
		ui::Padding{ PAD_Y, PAD_X, bottom_pad, PAD_X }
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

	bool draw_config_preview_header =
		components::configs::selected_config_tab ==
		"blur"; // only tab where we actually render the header, so offset for it (still need to reset it above
	            // otherwise to clear it, but rect creation is pointless tbf)

	if (draw_config_preview_header) {
		config_preview_content_container_rect.y = config_preview_header_container_rect.y2();
		config_preview_content_container_rect.h -= config_preview_header_container_rect.h;
	}

	ui::reset_container(
		config_preview_content_container,
		sdl::window,
		config_preview_content_container_rect,
		fonts::dejavu.height(),
		ui::Padding{ draw_config_preview_header ? 0 : PAD_Y, PAD_X, bottom_pad, PAD_X }
	);

	gfx::Rect option_information_container_rect = config_preview_container_rect;
	ui::Padding option_information_padding{ PAD_Y, PAD_X, bottom_pad, PAD_X };

	if (components::configs::selected_config_tab == "presets") {
		// presets tab takes up the whole space so show option info in the same area
		option_information_container_rect = config_container_rect;
		option_information_padding.top += ui::tabs_height(fonts::dejavu) + config_container_element_gap;
	}

	ui::reset_container(
		option_information_container,
		sdl::window,
		option_information_container_rect,
		config_container_element_gap,
		option_information_padding
	);

	gfx::Rect notification_container_rect = rect;
	notification_container_rect.w = ui::NOTIFICATION_DEFAULT_W;
	notification_container_rect.x =
		rect.x2() - notification_container_rect.w - components::notifications::NOTIFICATIONS_PAD_X;
	notification_container_rect.h = 300;
	notification_container_rect.y = components::notifications::NOTIFICATIONS_PAD_Y;

	ui::reset_container(notification_container, sdl::window, notification_container_rect, 6, {});

	gfx::Rect update_container_rect = rect;
	update_container_rect.w = ui::NOTIFICATION_DEFAULT_W;
	update_container_rect.x = rect.x2() - update_container_rect.w - PAD_X;

	ui::reset_container(
		update_container, sdl::window, update_container_rect, 6, ui::Padding{ 0, 0, nav_container_rect.h, 0 }
	);

	bool render_corner_update_notice = components::update_notice::is_updating();
	ui::AnimatedElement* config_back_button = nullptr;

	// built first so it gets escape before the screens do
	ui::dialog::build(sdl::window, rect);

	switch (screen) {
		case Screens::TEST: {
			components::test::screen(main_container, delta_time);

			ui::add_button(
				"test back navigation",
				navigation_button_container,
				"",
				fonts::dejavu,
				[] {
					screen = Screens::MAIN;
				},
				{},
				icons::BACK
			);
			break;
		}
		case Screens::MAIN: {
			if (components::configs::should_load_config) {
				components::configs::loaded_config = false;
			}

			auto main_screen = components::main::screen(main_container, delta_time);

			if (initialisation_res) {
				switch (main_screen) {
					case components::main::MainScreen::HOME: {
#ifdef _DEBUG
						ui::add_button("test button", navigation_button_container, "Test", fonts::dejavu, [] {
							screen = Screens::TEST;
						});
#endif
						break;
					}
					case components::main::MainScreen::PENDING: {
						const auto& pending = tasks::get_pending_copy();

						ui::add_button("start button", nav_container, "Start", fonts::dejavu, [] {
							tasks::start_pending_videos();
						});

						// r = start rendering
						if (keys::is_key_pressed(SDL_SCANCODE_R)) {
							tasks::start_pending_videos();
						}

						ui::set_next_same_line(nav_container);
						ui::add_button("cancel button", nav_container, "Cancel", fonts::dejavu, [] {
							tasks::cancel_all_pending();
						});

						// escape = cancel
						if (keys::is_key_pressed(SDL_SCANCODE_ESCAPE)) {
							tasks::cancel_all_pending();
						}

						ui::set_next_same_line(nav_container);
						components::main::open_files_button(nav_container, "Add files");

						break;
					}
					case components::main::MainScreen::PROGRESS: {
						auto current_render = rendering::video_render_queue.front();
						if (current_render) {
							auto progress = current_render->state->get_progress();
							if (!progress.rendered_a_frame && !current_render->state->is_paused()) {
								// keep redrawing so the loading spinner animates while initialising / building the
								// tensorrt engine
								want_to_render = true;
							}

							ui::add_button(
								current_render->state->is_paused() ? "resume render button" : "pause render button",
								nav_container,
								current_render->state->is_paused() ? "Resume" : "Pause",
								fonts::dejavu,
								[] {
									auto current_render = rendering::video_render_queue.front();
									current_render->state->toggle_pause();
								}
							);
						}

						ui::set_next_same_line(nav_container);
						ui::add_button("stop render button", nav_container, "Cancel", fonts::dejavu, [] {
							auto current_render = rendering::video_render_queue.front();
							current_render->state->stop();
						});

						ui::set_next_same_line(nav_container);
						components::main::open_files_button(nav_container, "Add files");

						break;
					}
				}

				ui::set_next_same_line(nav_container);
				ui::add_button(
					"configuration navigation",
					nav_container,
					"Config",
					fonts::dejavu,
					[] {
						screen = Screens::CONFIG;
					},
					{},
					icons::SETTINGS
				);
			}

			ui::center_elements_in_container(main_container);

			render_corner_update_notice |= main_screen == components::main::MainScreen::HOME;

			break;
		}
		case Screens::CONFIG: {
			components::configs::should_load_config = true;

			components::configs::screen(
				config_container,
				nav_container,
				config_preview_header_container,
				config_preview_content_container,
				option_information_container,
				delta_time
			);

			config_back_button = ui::add_button(
				"config back navigation",
				navigation_button_container,
				"",
				fonts::dejavu,
				[] {
					if (!components::configs::has_unsaved_changes()) {
						screen = Screens::MAIN;
						return;
					}

					ui::dialog::confirm_destructive(
						"Discard unsaved changes?",
						"Going back will discard your unsaved config changes.",
						"Discard",
						[] {
							screen = Screens::MAIN;
						}
					);
				},
				{},
				icons::BACK
			);

			ui::center_elements_in_container(config_preview_header_container, true, false);
			ui::center_elements_in_container(config_preview_content_container);
			ui::center_elements_in_container(option_information_container, true, false);

			// the app tab draws its own update notice
			if (components::configs::selected_config_tab == "app")
				render_corner_update_notice = false;

			break;
		}
	}

	if (render_corner_update_notice) {
		components::update_notice::render(update_container);
		ui::anchor_elements_to_bottom(update_container);
	}

	// config preview cleanup TODO: hate this code pattern? how else do i do this nicely tho?
	static Screens last_screen = screen;
	if (last_screen != screen) {
		if (last_screen == Screens::CONFIG)
			components::configs::reset_config_preview();

		// the preset may have just been changed in the config tab, and whether trimming is possible depends on it
		components::main::invalidate_trim_support();

		last_screen = screen;
	}

	components::notifications::render(notification_container);

	ui::center_elements_in_container(nav_container);

	want_to_render |= ui::update_container_frame(notification_container, delta_time);
	want_to_render |= ui::update_container_frame(update_container, delta_time);
	want_to_render |= ui::update_container_frame(nav_container, delta_time);
	want_to_render |= ui::update_container_frame(navigation_button_container, delta_time);

	want_to_render |= ui::update_container_frame(main_container, delta_time);
	want_to_render |= ui::update_container_frame(config_container, delta_time);
	want_to_render |= ui::update_container_frame(config_preview_header_container, delta_time);
	want_to_render |= ui::update_container_frame(config_preview_content_container, delta_time);
	want_to_render |= ui::update_container_frame(option_information_container, delta_time);

	ui::stick_element_to_top(config_container, config_back_button);

	want_to_render |= ui::dialog::update_frame(delta_time);
	want_to_render |= ui::tooltip::update(delta_time);
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
		ui::render_container(navigation_button_container);
		ui::render_container(update_container);
		ui::render_container(notification_container);

		ui::dialog::render();

		ui::tooltip::render();

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

void gui::renderer::on_render_finished(
	const rendering::VideoRenderDetails& render,
	const tl::expected<rendering::RenderResult, std::variant<std::string, rendering::RenderError>>& result
) {
	std::string video_name = u::path_to_string(render.input_path.stem());

	if (!result) {
		components::notifications::show_failure_notification(
			std::format("Render '{}' failed.", video_name), result.error(), std::nullopt
		);

		auto app_config = config_app::get_app_config();
		if (app_config.render_failure_notifications) {
			desktop_notification::show("Blur render failed", std::format("Failed to render video {}", video_name));
		}

		return;
	}

	if (result->stopped) {
		gui::components::notifications::add(std::format("Render '{}' stopped", video_name), ui::NotificationType::INFO);
		return;
	}

	gui::components::notifications::add(
		std::format("Render '{}' completed", video_name),
		ui::NotificationType::SUCCESS,
		[output_path = result->output_path](const std::string& id) {
			std::string file_url = std::format("file://{}", u::path_to_string(output_path));
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
