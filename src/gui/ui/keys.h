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

	// consecutive click count for the last mouse press (2 = double click, 3 = triple, ...), straight from SDL so
	// it uses the OS double-click interval. only meaningful while the button is down.
	inline std::unordered_map<std::uint8_t, int> mouse_click_counts;

	bool process_event(const SDL_Event& event);

	int get_click_count(std::uint8_t button = SDL_BUTTON_LEFT);

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
