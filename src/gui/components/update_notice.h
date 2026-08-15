#pragma once

#include "common/updates.h"
#include "../ui/ui.h"

namespace gui::components::update_notice {
	// call from anywhere to show update notice
	void set_available(const updates::UpdateCheckRes& update);

	bool is_updating();

	void render(ui::Container& container);
}
