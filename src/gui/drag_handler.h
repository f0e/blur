
#pragma once

namespace gui::drag_handler {
	inline bool dragging = false;
	inline gfx::Point drag_position;

	class DragTarget : public os::DragTarget {
	public:
		void dragEnter(os::DragEvent& ev) override;
		void dragLeave(os::DragEvent& ev) override;
		void drag(os::DragEvent& ev) override;
		void drop(os::DragEvent& ev) override;
	};
}
