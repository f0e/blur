#include "keys.h"
#include "gui/ui/ui.h"

bool keys::process_event(const SDL_Event& event) {
	if (ui::get_active_element() &&
	    ui::helpers::text_input::has_active_text_edit(ui::get_active_element()->element->id))
	{
		switch (event.type) {
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_TEXT_EDITING:
			// case SDL_EVENT_TEXT_EDITING_EXT:
			case SDL_EVENT_TEXT_INPUT:
				ui::text_event_queue.push_back(event);
				return true;

			default:
				break;
		}
	}

	ui::event_queue.push_back(event);

	switch (event.type) {
		case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
			mouse_pos = { -1, -1 };
			pressed_mouse_keys
				.clear(); // fix mouseup not being registered when left the window todo: handle this properly
			held_mouse_keys.clear();
			return true;
		}

		case SDL_EVENT_MOUSE_MOTION: {
			mouse_pos = { static_cast<int>(event.motion.x), static_cast<int>(event.motion.y) };
			return true;
		}

		case SDL_EVENT_MOUSE_BUTTON_DOWN: {
			// mouse_pos = position; // TODO: assuming this is inaccurate too
			pressed_mouse_keys.insert(event.button.button);
			return true;
		}

		case SDL_EVENT_MOUSE_BUTTON_UP: {
			// mouse_pos = position; // TODO: this is inaccurate? if you press open file button move cursor off screen
			// then close the picker there'll be a mouseup event with mouse pos still on the button
			pressed_mouse_keys.erase(event.button.button);
			held_mouse_keys.erase(event.button.button);
			return true;
		}

		case SDL_EVENT_KEY_DOWN: {
			pressing_keys.insert(event.key.scancode);
			return true;
		}
		case SDL_EVENT_KEY_UP: {
			pressing_keys.erase(event.key.scancode);
			return true;
		}

		case SDL_EVENT_MOUSE_WHEEL: {
			// TODO:
			// if (event.wheel.type()) // trackpad
			// 	scroll_delta_precise = event.wheelDelta().y;
			// else // mouse
			scroll_delta = -event.wheel.y * 1500.f;
			// todo: better trackpad scrolling (https://github.com/libsdl-org/SDL/pull/5382)
			return true;
		}

		default:
			return false;
	}

	return false;
}

void keys::on_frame_start() {
	// Clear the handled keys set at the beginning of each frame
	// This should be called at the start of each frame update
	handled_keys.clear();
}

void keys::on_mouse_press_handled(std::uint8_t button) { // TODO:
	// somethings been pressed, count it as we're not pressing the button anymore
	// todo: is this naive and stupid? it seems kinda elegant, i cant think of a situation
	// where you'd want to press two things with one click
	pressed_mouse_keys.erase(button);
	held_mouse_keys.insert(button);
}

void keys::on_key_press_handled(std::uint8_t scancode) {
	// Mark the key as handled for this frame
	handled_keys.insert(scancode);
}

bool keys::is_rect_pressed(const gfx::Rect& rect, std::uint8_t button) {
	return rect.contains(mouse_pos) && is_mouse_down(button);
}

bool keys::is_mouse_down(std::uint8_t button) {
	return pressed_mouse_keys.contains(button);
}

bool keys::is_mouse_pressed(std::uint8_t button) {
	return pressed_mouse_keys.contains(button) && !held_mouse_keys.contains(button);
}

bool keys::is_mouse_dragging(std::uint8_t button) {
	return pressed_mouse_keys.contains(button) || held_mouse_keys.contains(button);
}

bool keys::is_key_down(std::uint8_t scancode) {
	return pressing_keys.contains(scancode);
}

bool keys::is_key_pressed(std::uint8_t scancode) {
	// Check if the key is in the pressing set but not in the handled set
	return pressing_keys.contains(scancode) && !handled_keys.contains(scancode);
}
