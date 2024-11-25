
#include "event_handler.h"

#include "gui.h"

#include "ui/keys.h"

bool gui::event_handler::process_event(const os::Event& event) {
	switch (event.type()) {
		case os::Event::CloseApp:
		case os::Event::CloseWindow: {
			closing = true;
			return false;
		}

		case os::Event::ResizeWindow:
			break;

		case os::Event::MouseLeave: {
			keys::on_mouse_leave();
			return true;
		}

		case os::Event::MouseMove: {
			keys::on_mouse_move(event.position(), event.modifiers(), event.pointerType(), event.pressure());
			return true;
		}

		case os::Event::MouseDown: {
			keys::on_mouse_down(
				event.position(), event.button(), event.modifiers(), event.pointerType(), event.pressure()
			);
			return true;
		}

		case os::Event::MouseUp: {
			keys::on_mouse_up(event.position(), event.button(), event.modifiers(), event.pointerType());
			return true;
		}

		default:
			break;
	}

	return false;
}

bool gui::event_handler::generate_messages_from_os_events(bool rendered_last
) { // https://github.com/aseprite/aseprite/blob/45c2a5950445c884f5d732edc02989c3fb6ab1a6/src/ui/manager.cpp#L393
	const static int default_tickrate = 60;
	const static double default_timeout = 1.f / default_tickrate;

	bool processed_an_event = false;

	double timeout = rendered_last
	                     ? 0.0              // < an animation is playing or something, so be quick
	                     : default_timeout; // < nothing's happening so take your time - but still be fast enough for
	                                        // the ui to update occasionally to see if it wants to render

	while (true) {
		os::Event event;

		event_queue->getEvent(event, timeout);

		if (event.type() == os::Event::None)
			break;

		if (process_event(event))
			processed_an_event = true;

		timeout =
			0.0; // i think this is correct, allow blocking for first event but any subsequent one should be instant
	}

	return processed_an_event;
}
