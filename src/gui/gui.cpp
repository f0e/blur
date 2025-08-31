#include "gui.h"
#include "gui/tasks.h"
#include "renderer.h"
#include "sdl.h"
#include "ui/keys.h"
#include "ui/ui.h"

#define DEBUG_RENDER_LOGGING 0

namespace {
	const int PAD_X = 24;
	const int PAD_Y = PAD_X;
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

	while (true) {
		auto frame_start = std::chrono::steady_clock::now();

		sdl::update_vsync();

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
					std::vector<std::filesystem::path> paths = { u::string_to_path(event.drop.data) };

					if (gui::renderer::screen == gui::renderer::Screens::CONFIG) {
						auto sample_video_path = blur.settings_path / "sample_video.mp4";
						bool sample_video_exists = std::filesystem::exists(sample_video_path);
						if (!sample_video_exists) {
							tasks::add_sample_video(paths[0]);

							break;
						}
					}

					tasks::add_files_for_render(paths);

					break;
				}

				default:
					break;
			}

			ui::handle_videos_event(event, to_render);

			if (keys::process_event(event)) {
				ui::on_update_input_start();

				to_render |= ui::update_container_input(renderer::notification_container);
				to_render |= ui::update_container_input(renderer::nav_container);

				to_render |= ui::update_container_input(renderer::main_container);
				to_render |= ui::update_container_input(renderer::config_container);
				to_render |= ui::update_container_input(renderer::option_information_container);
				to_render |= ui::update_container_input(renderer::config_preview_header_container);
				to_render |= ui::update_container_input(renderer::config_preview_content_container);

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
				SDL_Delay(time_to_sleep);
		}
		else {
			rendered_last = false;
			SDL_Delay(sdl::TICKRATE_MS);
		}
	}
}
