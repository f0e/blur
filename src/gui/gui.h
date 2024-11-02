#pragma once

namespace gui {
	class DragTarget : public os::DragTarget {
	public:
		void dragEnter(os::DragEvent& ev) override;
		void dragLeave(os::DragEvent& ev) override;
		void drag(os::DragEvent& ev) override;
		void drop(os::DragEvent& ev) override;
	};

	struct WindowData {
		bool dragging = false;
		gfx::Point dragPosition;
		gfx::Rect dropZone;
	};

	inline WindowData windowData;

	inline os::WindowRef window;
	inline bool queue_redraw = false;

	void redraw_window(os::Window* window);
	void run();
}
