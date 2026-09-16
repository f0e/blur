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
		float delta_time,
		bool& is_progress_shown,
		float& bar_percent
	);

	void render_pending(
		ui::Container& container,
		ui::Container& config_container,
		ui::Container& queue_container,
		const std::vector<std::shared_ptr<tasks::PendingVideo>>& pending
	);

	void invalidate_trim_support();

	// which subscreen screen() last drew, nothing if the main screen isn't the one up
	[[nodiscard]] std::optional<MainScreen> current_screen();

	// videos can be queued while something's rendering, so both screens can be up at once - this is the one
	// switching away from the current screen would land on, when there is one
	[[nodiscard]] std::optional<MainScreen> get_screen_switch_target();

	// asks for a screen for the next frame. only PROGRESS and PENDING can be asked for, HOME is what's left when
	// there's nothing to render or queue
	void show_screen(MainScreen main_screen);

	void render_home(ui::Container& container);

	MainScreen screen(
		ui::Container& container,
		ui::Container& queue_config_container,
		ui::Container& queue_container,
		float delta_time
	);
}
