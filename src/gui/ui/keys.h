#pragma once

namespace keys {
	inline gfx::Point mouse_pos;
	inline std::unordered_set<std::uint8_t> pressed_mouse_keys;
	inline std::unordered_set<std::uint8_t> held_mouse_keys; // this feels wrong but it works
	inline std::unordered_set<std::uint8_t> pressing_keys;
	inline std::unordered_set<std::uint8_t> handled_keys;

	inline float scroll_delta = 0.f;
	inline float scroll_x_delta = 0.f;
	inline bool scroll_is_horizontal = false;

	inline bool mouse_captured = false; // something's being dragged, keep tracking the mouse outside the window

	// consecutive click count (2 = double click, 3 = triple, ...). only meaningful while the button is down
	inline std::unordered_map<std::uint8_t, int> mouse_click_counts;
	// increasing press ids, so a control can tell whether it handled the previous press
	inline std::unordered_map<std::uint8_t, std::uint64_t> mouse_press_ids;

	bool process_event(const SDL_Event& event);

	int get_click_count(std::uint8_t button = SDL_BUTTON_LEFT);
	std::uint64_t get_mouse_press_id(std::uint8_t button = SDL_BUTTON_LEFT);

	void set_mouse_capture(bool capture);

	// for when something else (like a native file drag) swallowed the release
	void forget_mouse_buttons();

	void on_frame_start();

	void on_mouse_press_handled(std::uint8_t button);
	void on_key_press_handled(std::uint8_t scancode);

	bool is_rect_pressed(const gfx::Rect& rect, std::uint8_t button);
	bool is_mouse_down(std::uint8_t button = SDL_BUTTON_LEFT);
	bool is_mouse_pressed(std::uint8_t button = SDL_BUTTON_LEFT);
	bool is_mouse_dragging(std::uint8_t button = SDL_BUTTON_LEFT);

	bool is_key_down(std::uint8_t scancode);
	bool is_key_pressed(std::uint8_t scancode);
}
