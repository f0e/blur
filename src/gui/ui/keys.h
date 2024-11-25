#pragma once

namespace keys {
	inline gfx::Point mouse_pos;
	inline std::unordered_set<os::Event::MouseButton> pressed_mouse_keys;

	void on_mouse_leave();
	void on_mouse_move(
		const gfx::Point& position, os::KeyModifiers modifiers, os::PointerType pointer_type, float pressure
	);
	void on_mouse_down(
		const gfx::Point& position,
		os::Event::MouseButton button,
		os::KeyModifiers modifiers,
		os::PointerType pointer_type,
		float pressure
	);
	void on_mouse_up(
		const gfx::Point& position,
		os::Event::MouseButton button,
		os::KeyModifiers modifiers,
		os::PointerType pointer_type
	);

	void on_mouse_press_handled(os::Event::MouseButton button);

	bool is_rect_pressed(const gfx::Rect& rect, os::Event::MouseButton button);
	bool is_mouse_down(os::Event::MouseButton button = os::Event::MouseButton::LeftButton);
}
