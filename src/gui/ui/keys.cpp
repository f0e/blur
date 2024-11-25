#include "keys.h"

void keys::on_mouse_leave() {
	mouse_pos = { -1, -1 };
}

void keys::on_mouse_move(
	const gfx::Point& position, os::KeyModifiers /*modifiers*/, os::PointerType /*pointer_type*/, float /*pressure*/
) {
	mouse_pos = position;
}

void keys::on_mouse_down(
	const gfx::Point& /*position*/,
	os::Event::MouseButton button,
	os::KeyModifiers /*modifiers*/,
	os::PointerType /*pointer_type*/,
	float /*pressure*/
) {
	// mouse_pos = position; // TODO: assuming this is inaccurate too
	pressed_mouse_keys.insert(button);
}

void keys::on_mouse_up(
	const gfx::Point& /*position*/,
	os::Event::MouseButton button,
	os::KeyModifiers /*modifiers*/,
	os::PointerType /*pointer_type*/
) {
	// mouse_pos = position; // TODO: this is inaccurate? if you press open file button move cursor off screen then
	// close the picker there'll be a mouseup event with mouse pos still on the button
	pressed_mouse_keys.erase(button);
}

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
