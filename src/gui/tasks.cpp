#include "tasks.h"

#include <common/rendering.h>
#include "gui.h"
#include "gui/renderer.h"

void tasks::run() {
	blur.initialise(false, true);

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

	rendering.set_render_finished_callback([](Render* render, bool success) {
		gui::renderer::on_render_finished(render, success);
	});

	while (true) {
		rendering.render_videos();
	}
}

void tasks::add_files(const std::vector<std::string>& files) {
	for (const auto& path : files) {
		u::log("dropped {}", path);

		rendering.queue_render(Render(path));
	}
}
