#pragma once

namespace keys {
	inline gfx::Point mouse_pos;
	inline std::unordered_set<os::Event::MouseButton> pressed_mouse_keys;

	void on_mouse_move(gfx::Point position, os::KeyModifiers modifiers, os::PointerType pointerType, float pressure);
	void on_mouse_down(gfx::Point position, os::Event::MouseButton button, os::KeyModifiers modifiers, os::PointerType pointerType, float pressure);
	void on_mouse_up(gfx::Point position, os::Event::MouseButton button, os::KeyModifiers modifiers, os::PointerType pointerType);

	void on_mouse_press_handled(os::Event::MouseButton button);

	bool is_rect_pressed(gfx::Rect rect, os::Event::MouseButton button);
	bool is_mouse_down(os::Event::MouseButton button = os::Event::MouseButton::LeftButton);
}
