#include "keys.h"
#include "gui/ui/ui.h"
#include "gui/render/render.h"
#include "gui/os/window.h"

namespace {
	void process_scroll_delta(float x, float y, bool precise, Uint64 timestamp) {
		static Uint64 last_wheel_timestamp = 0;
		static bool scroll_axis_locked = false;
		static float gesture_x_distance = 0.f;
		static float gesture_y_distance = 0.f;
		constexpr Uint64 scroll_gesture_timeout_ns = 150 * SDL_NS_PER_MS;

		Uint64 previous_wheel_timestamp = last_wheel_timestamp;
		bool new_gesture =
			previous_wheel_timestamp == 0 || timestamp - previous_wheel_timestamp > scroll_gesture_timeout_ns;
		if (new_gesture) {
			scroll_axis_locked = !precise;
			gesture_x_distance = 0.f;
			gesture_y_distance = 0.f;
		}

		gesture_x_distance += std::abs(x);
		gesture_y_distance += std::abs(y);
		if (!scroll_axis_locked) {
			keys::scroll_is_horizontal = gesture_x_distance > gesture_y_distance;

			constexpr float axis_lock_min_distance = 2.f;
			constexpr float axis_dominance = 1.25f;
			float total_distance = gesture_x_distance + gesture_y_distance;
			bool clearly_horizontal = gesture_x_distance > gesture_y_distance * axis_dominance;
			bool clearly_vertical = gesture_y_distance > gesture_x_distance * axis_dominance;
			scroll_axis_locked = total_distance >= axis_lock_min_distance && (clearly_horizontal || clearly_vertical);
		}
		else if (new_gesture) {
			keys::scroll_is_horizontal = gesture_x_distance > gesture_y_distance;
		}

		last_wheel_timestamp = timestamp;

		// Keep controls such as timeline zoom in wheel-like units. Containers use the unscaled point delta below.
		float control_scale = precise ? 0.1f : 1.f;
		keys::scroll_delta += keys::scroll_is_horizontal ? 0.f : -y * control_scale;
		keys::scroll_x_delta += keys::scroll_is_horizontal ? -x * control_scale : 0.f;
		// Vertical scroll views should always receive the native Y component. Axis locking is only for controls that
		// interpret wheel units (for example, horizontal timeline panning) and must not discard subtle list movement.
		if (precise) {
			float point_delta = -y / render::ui_scale;
			keys::precise_scroll_delta += point_delta;

			// Keep the latest native velocity for a continuous handoff from finger/momentum motion to the edge spring.
			// The first packet uses a conservative display-rate estimate because it has no preceding timestamp.
			float event_seconds =
				new_gesture ? 1.f / 60.f : (float)(timestamp - previous_wheel_timestamp) / SDL_NS_PER_SECOND;
			event_seconds = std::clamp(event_seconds, 1.f / 240.f, 1.f / 20.f);
			keys::precise_scroll_velocity = std::clamp(point_delta / event_seconds, -6000.f, 6000.f);
		}
	}
}

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

	os::window::ScrollEvent native_scroll;
	if (os::window::consume_native_scroll_event(event, native_scroll)) {
		keys::scroll_gesture_active = native_scroll.physical;
		keys::scroll_momentum_active = native_scroll.momentum;
		if (native_scroll.x != 0.f || native_scroll.y != 0.f)
			process_scroll_delta(native_scroll.x, native_scroll.y, native_scroll.precise, event.common.timestamp);
		return true;
	}

	switch (event.type) {
		case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
			if (mouse_captured)
				// mid-drag, we're still tracking the mouse outside the window. don't drop the drag
				return true;

			mouse_pos = { -1, -1 };
			pressed_mouse_keys
				.clear(); // fix mouseup not being registered when left the window todo: handle this properly
			held_mouse_keys.clear();
			return true;
		}

		case SDL_EVENT_WINDOW_FOCUS_LOST: {
			// we won't hear about the mouse being released, so don't leave anything mid-press (or mid-drag)
			pressed_mouse_keys.clear();
			held_mouse_keys.clear();
			pressing_keys.clear();
			return true;
		}

		case SDL_EVENT_MOUSE_MOTION: {
			mouse_pos = {
				static_cast<int>(event.motion.x / render::ui_scale),
				static_cast<int>(event.motion.y / render::ui_scale),
			};
			return true;
		}

		case SDL_EVENT_MOUSE_BUTTON_DOWN: {
			// mouse_pos = position; // TODO: assuming this is inaccurate too
			pressed_mouse_keys.insert(event.button.button);
			mouse_click_counts[event.button.button] = event.button.clicks;
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
			process_scroll_delta(event.wheel.x, event.wheel.y, false, event.wheel.timestamp);
			return true;
		}

		default:
			return false;
	}

	return false;
}

void keys::set_mouse_capture(bool capture) {
	if (capture == mouse_captured)
		return;

	mouse_captured = capture;

	if (!SDL_CaptureMouse(capture))
		u::log_error("failed to {} mouse capture: {}", capture ? "enable" : "disable", SDL_GetError());
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

int keys::get_click_count(std::uint8_t button) {
	auto it = mouse_click_counts.find(button);
	return it == mouse_click_counts.end() ? 0 : it->second;
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
