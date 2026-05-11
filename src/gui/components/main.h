#pragma once

#include "common/rendering.h"
#include "../ui/ui.h"
#include "../tasks.h"

namespace gui::components::main {
	void open_files_button(ui::Container& container, const std::string& label);

	enum class MainScreen {
		PROGRESS,
		PENDING,
		HOME
	};

	void render_progress(
		ui::Container& container,
		const rendering::VideoRenderDetails& render,
		size_t render_index,
		bool current,
		float delta_time,
		bool& is_progress_shown,
		float& bar_percent
	);

	void render_pending(ui::Container& container, const std::vector<std::shared_ptr<tasks::PendingVideo>>& pending);

	void render_home(ui::Container& container);

	MainScreen screen(ui::Container& container, float delta_time);
}
