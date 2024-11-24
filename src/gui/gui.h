#pragma once

namespace gui {
	class DragTarget : public os::DragTarget {
	public:
		void dragEnter(os::DragEvent& ev) override;
		void dragLeave(os::DragEvent& ev) override;
		void drag(os::DragEvent& ev) override;
		void drop(os::DragEvent& ev) override;
	};

	class Message {};

	typedef std::list<Message*> Messages;
	static Messages msg_queue;

	struct WindowData {
		bool dragging = false;
		gfx::Point dragPosition;
		gfx::Rect dropZone;
	};

	inline WindowData windowData;

	inline os::WindowRef window;
	inline bool queue_redraw = false;

	inline os::NativeCursor current_cursor;
	inline bool set_cursor_this_frame = false;

	void set_cursor(os::NativeCursor cursor);

	bool redraw_window(os::Window* window, bool force_render);
	void on_resize(os::Window* window);

	void update_vsync();

	bool generate_messages_from_os_events(bool rendered_last);
	void event_loop();

	void run();
}
