
#include "drag_handler.h"
#include "tasks.h"

void gui::drag_handler::DragTarget::dragEnter(os::DragEvent& ev) {
	// v.dropResult(os::DropOperation::None); // TODO: what does this do? is it needed?

	drag_position = ev.position();
	dragging = true;
}

void gui::drag_handler::DragTarget::dragLeave(os::DragEvent& ev) {
	// todo: not triggering on windows?
	drag_position = ev.position();
	dragging = false;
}

void gui::drag_handler::DragTarget::drag(os::DragEvent& ev) {
	drag_position = ev.position();
}

void gui::drag_handler::DragTarget::drop(os::DragEvent& ev) {
	ev.acceptDrop(true);

	if (ev.dataProvider()->contains(os::DragDataItemType::Paths)) {
		std::vector<std::string> paths = ev.dataProvider()->getPaths();
		tasks::add_files(paths);
	}

	dragging = false;
}
