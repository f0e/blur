
#include "drag_handler.h"
#include "gui.h"
#include "renderer.h"
#include "tasks.h"

void gui::drag_handler::DragTarget::dragEnter(os::DragEvent& ev) {
	if (!gui::initialisation_res || !gui::initialisation_res->success)
		return;

	// v.dropResult(os::DropOperation::None); // TODO: what does this do? is it needed?

	drag_position = ev.position();
	dragging = true;
}

void gui::drag_handler::DragTarget::dragLeave(os::DragEvent& ev) {
	if (!gui::initialisation_res || !gui::initialisation_res->success)
		return;

	// todo: not triggering on windows?
	drag_position = ev.position();
	dragging = false;
}

void gui::drag_handler::DragTarget::drag(os::DragEvent& ev) {
	if (!gui::initialisation_res || !gui::initialisation_res->success)
		return;

	drag_position = ev.position();
}

void gui::drag_handler::DragTarget::drop(os::DragEvent& ev) {
	if (!gui::initialisation_res || !gui::initialisation_res->success)
		return;

	ev.acceptDrop(true);

	if (ev.dataProvider()->contains(os::DragDataItemType::Paths)) {
		std::vector<std::string> paths = ev.dataProvider()->getPaths();

		switch (gui::renderer::screen) {
			case gui::renderer::Screens::MAIN: {
				tasks::add_files(paths);
				break;
			}
			case gui::renderer::Screens::CONFIG: {
				if (paths.size() != 1) {
					gui::renderer::add_notification(
						"Too many videos dropped, only one video can be used as a config preview.",
						ui::NotificationType::NOTIF_ERROR
					);

					break;
				}

				tasks::add_sample_video(paths[0]);
				break;
			}
		}
	}

	dragging = false;
}
