#include "tasks.h"

#include "common/rendering.h"
#include "common/config_app.h"

#include "gui.h"
#include "gui/renderer.h"
#include "gui/ui/ui.h"

#include "components/main.h"
#include "components/notifications.h"
#include "components/update_notice.h"
#include "components/configs/configs.h"

namespace {
	size_t pending_index = 0;
	std::vector<std::shared_ptr<tasks::PendingVideo>> pending_videos;
	std::mutex pending_videos_mutex;

	void queue_render(const std::shared_ptr<tasks::PendingVideo>& pending_video) {
		auto app_config = config_app::get_app_config();

		auto queue_config_res = rendering::video_render_queue.add(
			pending_video->video_path,
			*pending_video->video_info,
			{},
			app_config,
			{},
			pending_video->start,
			pending_video->end,
			{},
			[](const rendering::VideoRenderDetails& render,
		       const tl::expected<rendering::RenderResult, std::variant<std::string, rendering::RenderError>>& result) {
				gui::renderer::on_render_finished(render, result);
			},
			pending_video->config_name,
			pending_video->mask,
			pending_video->auto_mask
		);

		if (queue_config_res.error) {
			gui::components::notifications::add(
				std::format(
					"Failed to queue '{}' for render: {}", pending_video->video_path.stem(), *queue_config_res.error
				),
				ui::NotificationType::NOTIF_ERROR,
				{},
				std::chrono::duration<float>(6.f)
			);
		}
	}
}

void tasks::run(const std::vector<std::string>& arguments) {
	gui::initialisation_res = blur.initialise(false, true);

	auto update_res = Blur::check_updates();
	if (update_res) {
		gui::components::update_notice::set_available(*update_res);
	}

	std::vector<std::filesystem::path> paths;
	paths.reserve(arguments.size());
	for (const auto& argument : arguments) {
		paths.emplace_back(u::string_to_path(argument));
	}

	add_files(paths); // todo: mac packaged app support (& linux? does it work?)

	std::thread video_info_thread([] {
		while (!blur.exiting) {
			process_pending_files();

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	});

	while (!blur.exiting) {
		if (!rendering::video_render_queue.process_next()) {
			finished_renders = 0;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		else {
			finished_renders++;
		}
	}

	video_info_thread.join();
}

void tasks::add_files(const std::vector<std::filesystem::path>& path_strs) {
	std::lock_guard<std::mutex> lock(pending_videos_mutex);

	for (const auto& path_str : path_strs) {
		std::filesystem::path path = std::filesystem::canonical(path_str);
		if (path.empty() || !std::filesystem::exists(path))
			continue;

		u::log("queueing {}", path);

		bool video_already_queued = false;
		for (const auto& pending_video : pending_videos) {
			if (path != pending_video->video_path)
				continue;

			video_already_queued = true;
			break;
		}

		if (video_already_queued) {
			gui::components::notifications::add(
				std::format("Video '{}' is already queued for rendering", path.filename()), ui::NotificationType::INFO
			);
			continue;
		}

		if (gui::renderer::screen != gui::renderer::Screens::MAIN) {
			gui::components::notifications::add(
				std::format("Queued '{}' for rendering", path.filename()), ui::NotificationType::INFO
			);
		}

		static size_t next_video_id = 0;

		// start from the config this video resolves to, and the masks that config asks for. the queue
		// screen can change all three after
		auto resolved = config_blur::resolve_config(path, {});

		auto pending_video = std::make_shared<PendingVideo>(PendingVideo{
			.video_id = next_video_id++,
			.video_path = path,
			.config_name = resolved.name,
			.config_source = resolved.source,
			.config_rule_pattern = resolved.rule_pattern,
		});

		if (!resolved.name.empty()) {
			auto config = config_blur::get_config(resolved.name);
			pending_video->mask = config.mask;
			pending_video->auto_mask = config.auto_mask;
		}

		pending_videos.push_back(std::move(pending_video));
	}
}

void tasks::process_pending_files() {
	std::unique_lock<std::mutex> lock(pending_videos_mutex);

	auto it = std::ranges::find_if(pending_videos, [](const auto& pv) {
		return !pv->video_info.has_value();
	});

	if (it == pending_videos.end()) {
		return;
	}

	auto video_path = (*it)->video_path;
	auto index = std::ranges::distance(pending_videos.begin(), it);

	auto video_info = u::get_video_info(video_path);

	if (index < pending_videos.size() && pending_videos[index]->video_path == video_path) {
		if (!video_info.has_video_stream) {
			gui::components::notifications::add(
				std::format("File is not a valid video or is unreadable: {}", video_path.filename()),
				ui::NotificationType::NOTIF_ERROR
			);
			pending_videos.erase(pending_videos.begin() + index);
		}
		else {
			pending_videos[index]->video_info = video_info;

			bool can_start = !pending_videos[index]->config_name.empty();

			if (can_start && (config_app::get_app_config().skip_queue || pending_videos[index]->start_immediately)) {
				auto pending_video = pending_videos[index];
				pending_videos.erase(pending_videos.begin() + index);

				queue_render(pending_video);
			}
		}
	}
}

void tasks::add_sample_video(const std::filesystem::path& path_str) {
	std::filesystem::path path = std::filesystem::canonical(path_str);
	if (path.empty() || !std::filesystem::exists(path))
		return;

	const auto video_info = u::get_video_info(path);
	if (!video_info.has_video_stream) {
		gui::components::notifications::add(
			std::format("File is not a valid video or is unreadable: {}", path.filename()),
			ui::NotificationType::NOTIF_ERROR
		);
		return;
	}

	gui::components::configs::set_sample_video(path);

	gui::components::notifications::add("Added sample video", ui::NotificationType::SUCCESS);
}

void tasks::cancel_all_pending() {
	std::lock_guard<std::mutex> lock(pending_videos_mutex);
	pending_videos.clear();
}

void tasks::cancel_pending(size_t video_id) {
	std::lock_guard<std::mutex> lock(pending_videos_mutex);
	std::erase_if(pending_videos, [video_id](const std::shared_ptr<PendingVideo>& pv) {
		return pv->video_id == video_id;
	});
}

void tasks::start_pending_videos() {
	std::lock_guard<std::mutex> lock(pending_videos_mutex);

	size_t missing_config = 0;

	std::erase_if(pending_videos, [&missing_config](const std::shared_ptr<PendingVideo>& pending_video) {
		if (pending_video->config_name.empty()) {
			missing_config++;
			return false;
		}

		if (!pending_video->video_info) {
			// not parsed yet, flag it to be started immediately when parsing finishes & don't erase
			pending_video->start_immediately = true;
			return false;
		}

		queue_render(pending_video);
		return true;
	});

	if (missing_config > 0) {
		gui::components::notifications::add(
			"missing config",
			missing_config == 1 ? "1 video still needs a config before it can render"
								: std::format("{} videos still need a config before they can render", missing_config),
			ui::NotificationType::NOTIF_ERROR
		);
	}
}

std::vector<std::shared_ptr<tasks::PendingVideo>> tasks::get_pending_copy() {
	return pending_videos;
}
