#include "gui.h"
#include "gui/tasks.h"
#include "renderer.h"
#include "sdl.h"
#include "ui/keys.h"
#include "ui/ui.h"
#include "components/notifications.h"
#include "components/configs/configs.h"
#include "components/configs/preview_frames.h"
#include "components/main.h"
#include "os/taskbar.h"

#define DEBUG_RENDER_LOGGING 0

namespace {
	const int PAD_X = 24;
	const int PAD_Y = PAD_X;

	// runs every tick rather than every drawn frame so the icon updates while minimised
	void update_taskbar_progress() {
		using os::taskbar::ProgressState;

		auto current = rendering::video_render_queue.front();

		if (current) {
			gui::render_failed = false;

			auto progress = current->state->get_progress();

			// no frame count yet (starting up, or building a tensorrt engine)
			if (!progress.rendered_a_frame || progress.total_frames <= 0) {
				os::taskbar::set_progress(ProgressState::INDETERMINATE);
				return;
			}

			os::taskbar::set_progress(
				current->state->is_paused() ? ProgressState::PAUSED : ProgressState::NORMAL,
				float(progress.current_frame) / float(progress.total_frames)
			);
			return;
		}

		if (gui::render_failed) {
			// keep the red bar until the window's focused
			if (sdl::window && !(SDL_GetWindowFlags(sdl::window) & SDL_WINDOW_INPUT_FOCUS)) {
				os::taskbar::set_progress(ProgressState::ERRORED, 1.f);
				return;
			}

			gui::render_failed = false;
		}

		os::taskbar::set_progress(ProgressState::NONE);
	}
}

int gui::run() {
	auto sdl_init_res = sdl::initialise();
	if (!sdl_init_res) {
		u::log_error("Error: {}", sdl_init_res.error());
		sdl::cleanup();
		return 1;
	}

	SDL_Event event;

	bool rendered_last = false;

	while (!blur.exiting) {
		auto frame_start = std::chrono::steady_clock::now();

		sdl::update_vsync();

		// check if app config was edited & we have to re-render
		to_render |= sdl::poll_config_reload();

		update_taskbar_progress();

		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_EVENT_QUIT:
					sdl::cleanup();
					return 0;

				case SDL_EVENT_WINDOW_EXPOSED:
					to_render = true;
					break;

				case SDL_EVENT_DROP_BEGIN:
					dragging = true;
					break;

				case SDL_EVENT_DROP_COMPLETE:
					dragging = false;
					break;

					// case SDL_EVENT_DROP_POSITION:
					// 	break;

				case SDL_EVENT_DROP_FILE: {
					std::filesystem::path path = u::string_to_path(event.drop.data);

					if (path.extension() == ".cfg") {
						u::log("loading config: {}", path);

						try {
							gui::components::configs::import_config(config_blur::parse(path));
						}
						catch (const std::exception& e) {
							gui::components::notifications::add(
								std::string("Failed to load config: ") + e.what(),
								ui::NotificationType::NOTIF_ERROR,
								{},
								std::chrono::duration<float>(3.f)
							);
						}

						break;
					}

					if (gui::renderer::screen == gui::renderer::Screens::CONFIG &&
					    !gui::components::configs::has_sample_video())
					{
						tasks::add_sample_video(path);

						break;
					}

					tasks::add_files({ path });

					break;
				}

				default:
					break;
			}

			ui::handle_videos_event(event, to_render);
			gui::components::configs::preview_frames::handle_event(event, to_render);
			gui::components::main::handle_event(event, to_render);

			if (keys::process_event(event)) {
				ui::on_update_input_start();
				to_render |= ui::update_container_input(renderer::notification_container);

				if (ui::dialog::is_open()) {
					// modal, nothing behind it gets input
					to_render |= ui::dialog::update_input();
				}
				else {
					to_render |= ui::update_container_input(renderer::history_panel_container);
					to_render |= ui::update_container_input(renderer::history_button_container);
					to_render |= ui::update_container_input(renderer::update_container);
					to_render |= ui::update_container_input(renderer::navigation_button_container);
					to_render |= ui::update_container_input(renderer::nav_container);

					to_render |= ui::update_container_input(renderer::main_container);
					to_render |= ui::update_container_input(renderer::queue_config_container);
					to_render |= ui::update_container_input(renderer::queue_container);
					to_render |= ui::update_container_input(renderer::config_container);
					to_render |= ui::update_container_input(renderer::option_information_container);
					to_render |= ui::update_container_input(renderer::config_preview_content_container);
				}

				ui::on_update_input_end();
			}
		}

		const bool rendered = renderer::redraw_window(
			rendered_last,
			to_render
		); // note: rendered isn't true if rendering was forced, it's only if an animation or smth is playing

#if DEBUG_RENDER_LOGGING
		static size_t frame = 0;
		u::log("{} rendered: {}, to render: {}", frame++, rendered, to_render);
#endif

		// vsync
		if (rendered) { // || to_render) { // im 99% sure this isn't needed
			to_render = false;
			rendered_last = true;

			auto elapsed_ms =
				std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - frame_start)
					.count();

			double time_to_sleep = sdl::vsync_frame_time_ms - elapsed_ms;
			if (time_to_sleep > 0.f)
				SDL_WaitEventTimeout(nullptr, time_to_sleep);
		}
		else {
			rendered_last = false;

			// wait on events rather than sleeping so pushed video frame events wake us up
			SDL_WaitEventTimeout(nullptr, (int)sdl::TICKRATE_MS);
		}
	}

	sdl::cleanup();
	return 0;
}
