#include "keys.h"

bool keys::process_event(const os::Event& event) {
	switch (event.type()) {
		case os::Event::MouseLeave: {
			mouse_pos = { -1, -1 };
			pressed_mouse_keys.clear(
			); // fix mouseup not being registered when left the window todo: handle this properly
			return true;
		}

		case os::Event::MouseMove: {
			mouse_pos = event.position();
			return true;
		}

		case os::Event::MouseDoubleClick:
		case os::Event::MouseDown: {
			// mouse_pos = position; // TODO: assuming this is inaccurate too
			pressed_mouse_keys.insert(event.button());
			return true;
		}

		case os::Event::MouseUp: {
			// mouse_pos = position; // TODO: this is inaccurate? if you press open file button move cursor off screen
			// then close the picker there'll be a mouseup event with mouse pos still on the button
			pressed_mouse_keys.erase(event.button());
			return true;
		}

		case os::Event::KeyDown: {
			pressed_keys.emplace_back(KeyPress{
				.scancode = event.scancode(),
				.modifiers = event.modifiers(),
				.unicode_char = (char)event.unicodeChar(),
				.repeat = event.repeat(),
			});
			return true;
		}

		case os::Event::MouseWheel: {
			if (event.preciseWheel()) // trackpad
				scroll_delta_precise = event.wheelDelta().y;
			else // mouse
				scroll_delta = event.wheelDelta().y * 2000.f;

			return true;
		}

		default:
			return false;
	}
}

void keys::on_input_end() {}

void keys::on_mouse_press_handled(os::Event::MouseButton button) {
	// somethings been pressed, count it as we're not pressing the button anymore
	// todo: is this naive and stupid? it seems kinda elegant, i cant think of a situation
	// where you'd want to press two things with one click
	pressed_mouse_keys.erase(button);
}

bool keys::is_rect_pressed(const gfx::Rect& rect, os::Event::MouseButton button) {
	return rect.contains(mouse_pos) && is_mouse_down(button);
}

bool keys::is_mouse_down(os::Event::MouseButton button) {
	return pressed_mouse_keys.contains(button);
}
