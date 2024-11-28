
#pragma once

namespace gui::event_handler {
	bool process_event(const os::Event& event);
	bool handle_events(bool rendered_last);
}
