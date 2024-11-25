
#pragma once

namespace gui::window_manager {
	os::WindowRef create_window(os::DragTarget& dragTarget);

	void on_resize(os::Window* window);
}
