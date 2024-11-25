
#pragma once

namespace gui::event_handler {
	bool process_event(const os::Event& event);
	bool generate_messages_from_os_events(bool rendered_last);
}
