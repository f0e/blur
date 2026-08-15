#pragma once

#include "common/updates.h"
#include "../ui/ui.h"

namespace gui::components::update_notice {
	// call from anywhere to show update notice
	void set_available(const updates::UpdateCheckRes& update);

	bool is_updating();

	// checks right now, whatever the check for updates setting says, and reports the result
	void check_now();
	bool is_checking();

	void render(ui::Container& container, ui::UpdateNoticeAlign align = ui::UpdateNoticeAlign::RIGHT);
}
