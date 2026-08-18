#include "gui.h"
#include "gui/tasks.h"
#include "renderer.h"
#include "sdl.h"
#include "ui/keys.h"
#include "ui/ui.h"
#include "components/notifications.h"
#include "components/configs/configs.h"

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

		// check if app config was edited & we have to re-render
		to_render |= sdl::poll_config_reload();

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
							const auto file_settings = config_blur::parse(path);

							ui::reset_tied_sliders();
							gui::components::configs::settings = file_settings;

							gui::components::configs::loaded_config = true;
							gui::components::configs::should_load_config = false;

							gui::renderer::screen = gui::renderer::Screens::CONFIG;

							gui::components::notifications::add(
								"Imported config", ui::NotificationType::INFO, {}, std::chrono::duration<float>(2.f)
							);
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
					    gui::components::configs::loaded_config && !gui::components::configs::has_sample_video())
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

			if (keys::process_event(event)) {
				ui::on_update_input_start();

				if (ui::dialog::is_open()) {
					// modal, nothing behind it gets input
					to_render |= ui::dialog::update_input();
				}
				else {
					to_render |= ui::update_container_input(renderer::notification_container);
					to_render |= ui::update_container_input(renderer::update_container);
					to_render |= ui::update_container_input(renderer::nav_container);

					to_render |= ui::update_container_input(renderer::main_container);
					to_render |= ui::update_container_input(renderer::config_container);
					to_render |= ui::update_container_input(renderer::option_information_container);
					to_render |= ui::update_container_input(renderer::config_preview_header_container);
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
				SDL_Delay(time_to_sleep);
		}
		else {
			rendered_last = false;
			SDL_Delay(sdl::TICKRATE_MS);
		}
	}
}
