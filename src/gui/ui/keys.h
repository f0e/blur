#pragma once

namespace keys {
	inline gfx::Point mouse_pos;
	inline std::unordered_set<os::Event::MouseButton> pressed_mouse_keys;

	struct KeyPress {
		os::KeyScancode scancode;
		os::KeyModifiers modifiers;
		char unicode_char;
		int repeat;
	};

	inline std::vector<KeyPress> pressed_keys;

	inline float scroll_delta = 0.f;

	bool process_event(const os::Event& event);
	void on_input_end();

	void on_mouse_press_handled(os::Event::MouseButton button);

	bool is_rect_pressed(const gfx::Rect& rect, os::Event::MouseButton button);
	bool is_mouse_down(os::Event::MouseButton button = os::Event::MouseButton::LeftButton);
}
