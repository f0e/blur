#include "tasks.h"

#include "common/rendering.h"
#include "common/config_app.h"

#include "gui.h"
#include "gui/renderer.h"
#include "gui/ui/ui.h"

#include "components/main.h"
#include "components/notifications.h"
#include "components/configs/configs.h"

namespace {
	std::filesystem::path video_player_path;
	std::vector<std::filesystem::path> pending_video_paths;
	std::mutex pending_video_paths_mutex;
}

void tasks::run(const std::vector<std::string>& arguments) {
	gui::initialisation_res = blur.initialise(false, true);

	rendering.set_progress_callback([] {
		std::optional<Render*> current_render_opt = rendering.get_current_render();
		if (current_render_opt) {
			Render& current_render = **current_render_opt;

			RenderStatus status = current_render.get_status();
			if (status.finished) {
				u::log("current render is finished, copying it so its final state can be displayed once by gui");

				// its about to be deleted, store a copy to be rendered at least once
				gui::components::main::current_render_copy = current_render;
				gui::to_render = true;
			}
		}

		// idk what you're supposed to do to trigger a redraw in a separate thread!!! I dont do gui!!! this works
		// tho :  ) todo: revisit this
		// TODO PORT:
		// ^ actually might not be needed since progress will update anyway
	});

	rendering.set_render_finished_callback([](Render* render, const tl::expected<RenderResult, std::string>& result) {
		gui::renderer::on_render_finished(render, result);
	});

	auto update_res = Blur::check_updates();
	if (update_res && !update_res->is_latest) {
		static const auto update_notification_duration = std::chrono::duration<float>(15.f);

#if defined(WIN32) || defined(__APPLE__)
		gui::components::notifications::add(
			std::format("There's a newer version ({}) available! Click to run the installer.", update_res->latest_tag),
			ui::NotificationType::INFO,
			[&](const std::string& id) {
				gui::components::notifications::close(id);

				const static std::string update_notification_id = "update progress notification";

				gui::components::notifications::add(
					update_notification_id,
					"Downloading update...",
					ui::NotificationType::INFO,
					{},
					std::chrono::duration<float>(gui::components::notifications::NOTIFICATION_LENGTH),
					false
				);

				std::thread([update_res] {
					Blur::update(update_res->latest_tag, [](const std::string& text, bool done) {
						gui::components::notifications::add(
							update_notification_id,
							text,
							ui::NotificationType::INFO,
							{},
							std::chrono::duration<float>(gui::components::notifications::NOTIFICATION_LENGTH),
							done
						);
					});

					blur.exiting = true;
				}).detach();
			},
			update_notification_duration
		);
#else
		gui::components::notifications::add(
			std::format(
				"There's a newer version ({}) available! Click to go to the download page.", update_res->latest_tag
			),
			ui::NotificationType::INFO,
			[&](const std::string& id) {
				SDL_OpenURL(update_res->latest_tag_url.c_str());
			},
			update_notification_duration
		);
#endif
	}

	std::vector<std::filesystem::path> paths;
	paths.reserve(arguments.size());
	for (const auto& argument : arguments) {
		paths.emplace_back(u::string_to_path(argument));
	}

	add_files_for_render(paths); // todo: mac packaged app support (& linux? does it work?)

	std::thread([] {
		while (!blur.exiting) {
			process_pending_files();

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	}).detach();

	while (!blur.exiting) {
		if (!rendering.render_next_video()) {
			finished_renders = 0;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		else {
			finished_renders++;
		}
	}
}

void tasks::add_files_for_render(const std::vector<std::filesystem::path>& path_strs) {
	std::lock_guard<std::mutex> lock(pending_video_paths_mutex);

	for (const auto& path_str : path_strs) {
		std::filesystem::path path = std::filesystem::canonical(path_str);
		if (path.empty() || !std::filesystem::exists(path))
			continue;

		u::log("queueing {}", path);

		gui::components::notifications::add(
			std::format("Queued '{}' for rendering", path.filename()), ui::NotificationType::INFO
		);

		pending_video_paths.push_back(path);
	}
}

void tasks::process_pending_files() {
	std::vector<std::filesystem::path> video_paths_to_process;

	{
		std::lock_guard<std::mutex> lock(pending_video_paths_mutex);
		if (pending_video_paths.empty())
			return;

		video_paths_to_process = std::move(pending_video_paths);
		pending_video_paths.clear();
	}

	auto app_config = config_app::get_app_config();

	for (auto& video_path : video_paths_to_process) {
		auto video_info = u::get_video_info(video_path);

		Render render(video_path, video_info);

		if (!video_info.has_video_stream) {
			gui::components::notifications::add(
				std::format("File is not a valid video or is unreadable: {}", render.get_input_video_path()),
				ui::NotificationType::NOTIF_ERROR
			);
			continue;
		}

		if (app_config.notify_about_config_override) {
			if (!render.is_global_config())
				gui::components::notifications::add(
					"Using override config from video folder", ui::NotificationType::INFO
				);
		}

		rendering.queue_render(std::move(render));
	}
}

void tasks::add_sample_video(const std::filesystem::path& path_str) {
	std::filesystem::path path = std::filesystem::canonical(path_str);
	if (path.empty() || !std::filesystem::exists(path))
		return;

	auto sample_video_path = blur.settings_path / "sample_video.mp4";

	// todo: reencode?
	std::filesystem::copy(path, sample_video_path);

	gui::components::notifications::add("Added sample video", ui::NotificationType::SUCCESS);

	gui::components::configs::just_added_sample_video = true;
}

void tasks::set_video_player_path(const std::filesystem::path& path_str) {
	video_player_path = path_str;
}
