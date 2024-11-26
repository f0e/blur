#include "tasks.h"

#include <common/rendering.h>
#include "gui.h"

void tasks::run() {
	blur.initialise(false, true);

	rendering.set_progress_callback([] {
		if (gui::window) {
			// idk what you're supposed to do to trigger a redraw in a separate thread!!! I dont do gui!!! this works
			// tho :  ) todo: revisit this
			os::Event event;
			gui::window->queueEvent(event);
		}
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
