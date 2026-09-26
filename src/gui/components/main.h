#pragma once

#include "common/rendering.h"
#include "../ui/ui.h"
#include "../tasks.h"

namespace gui::components::main {
	void open_files_button(ui::Container& container, const std::string& label);

	enum class MainScreen : uint8_t {
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

	// frees the queue's previews, for when the queue isn't shown
	void release_previews();

	void handle_event(const SDL_Event& event, bool& to_render);

	// which subscreen was last drawn, if the main screen is up
	[[nodiscard]] std::optional<MainScreen> current_screen();

	// both screens can be up at once, this is the one to switch to
	[[nodiscard]] std::optional<MainScreen> get_screen_switch_target();

	// only PROGRESS and PENDING can be asked for
	void show_screen(MainScreen main_screen);

	void render_home(ui::Container& container);

	MainScreen screen(
		ui::Container& container,
		ui::Container& queue_config_container,
		ui::Container& queue_container,
		float delta_time
	);
}
