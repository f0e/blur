
#include "event_handler.h"

#include "gui.h"
#include "gui/tasks.h"
#include "renderer.h"

#include "ui/keys.h"

bool gui::event_handler::process_event(const os::Event& event) {
	switch (event.type()) {
		case os::Event::CloseApp:
		case os::Event::CloseWindow: {
			closing = true;
			return false;
		}

		case os::Event::DropFiles: {
			// mac dropping files onto executable (todo: when else is this triggered?)
			DEBUG_LOG("file drop event");
			tasks::add_files(event.files());
			break;
		}

		default:
			break;
	}

	bool updated = false;

	if (keys::process_event(event)) {
		ui::on_update_input_start();

		updated |= ui::update_container_input(renderer::notification_container);
		updated |= ui::update_container_input(renderer::nav_container);

		updated |= ui::update_container_input(renderer::main_container);
		updated |= ui::update_container_input(renderer::config_container);
		updated |= ui::update_container_input(renderer::option_information_container);
		updated |= ui::update_container_input(renderer::config_preview_container);

		ui::on_update_input_end();
	}

	keys::on_input_end();

	return updated;
}

bool gui::event_handler::handle_events(bool rendered_last
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
