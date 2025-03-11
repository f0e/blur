#include "tasks.h"

#include <common/rendering.h>
#include "gui.h"
#include "gui/renderer.h"
#include "gui/ui/ui.h"

void tasks::run(const std::vector<std::string>& arguments) {
	auto res = blur.initialise(false, true);

	gui::initialisation = res;

	rendering.set_progress_callback([] {
		if (!gui::window)
			return;

		std::optional<Render*> current_render_opt = rendering.get_current_render();
		if (current_render_opt) {
			Render& current_render = **current_render_opt;

			RenderStatus status = current_render.get_status();
			if (status.finished) {
				u::log("current render is finished, copying it so its final state can be displayed once by gui");

				// its about to be deleted, store a copy to be rendered at least once
				gui::renderer::current_render_copy = current_render;
				gui::to_render = true;
			}
		}

		// idk what you're supposed to do to trigger a redraw in a separate thread!!! I dont do gui!!! this works
		// tho :  ) todo: revisit this
		os::Event event;
		gui::window->queueEvent(event);
	});

	rendering.set_render_finished_callback([](Render* render, const RenderResult& result) {
		gui::renderer::on_render_finished(render, result);
	});

	add_files(arguments); // todo: mac packaged app support (& linux? does it work?)

	while (true) {
		rendering.render_videos();
	}
}

void tasks::add_files(const std::vector<std::string>& files) {
	for (const std::string& path_str : files) {
		std::filesystem::path path = std::filesystem::canonical(path_str);
		if (path.empty() || !std::filesystem::exists(path))
			continue;

		u::log("queueing {}", path.string());

		Render render(path);

		if (gui::renderer::screen != gui::renderer::Screens::MAIN)
			gui::renderer::add_notification(
				std::format("Queued '{}' for rendering", base::to_utf8(render.get_video_name())),
				ui::NotificationType::INFO
			);

		rendering.queue_render(std::move(render));
	}
}
