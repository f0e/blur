#include "keys.h"

void keys::on_mouse_move(gfx::Point position, os::KeyModifiers modifiers, os::PointerType pointerType, float pressure) {
	mouse_pos = position;
}

void keys::on_mouse_down(gfx::Point position, os::Event::MouseButton button, os::KeyModifiers modifiers, os::PointerType pointerType, float pressure) {
	mouse_pos = position;
	pressed_mouse_keys.insert(button);
}

void keys::on_mouse_up(gfx::Point position, os::Event::MouseButton button, os::KeyModifiers modifiers, os::PointerType pointerType) {
	mouse_pos = position;
	pressed_mouse_keys.erase(button);
}

void keys::on_mouse_press_handled(os::Event::MouseButton button) {
	// somethings been pressed, count it as we're not pressing the button anymore
	// todo: is this naive and stupid? it seems kinda elegant, i cant think of a situation
	// where you'd want to press two things with one click
	pressed_mouse_keys.erase(button);
}

bool keys::is_rect_pressed(gfx::Rect rect, os::Event::MouseButton button) {
	return rect.contains(mouse_pos) && is_mouse_down(button);
}

bool keys::is_mouse_down(os::Event::MouseButton button) {
	return pressed_mouse_keys.contains(button);
}
