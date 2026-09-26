#pragma once

#include "../ui/ui.h"
#include "common/rendering.h"

namespace gui::components::render_history {
	inline constexpr int BUTTON_SIZE = 24;

	// this session's renders, newest first. not persisted anywhere
	void add_success(const rendering::VideoRenderDetails& render, const rendering::RenderResult& result);
	void add_failure(
		const rendering::VideoRenderDetails& render, const std::variant<std::string, rendering::RenderError>& error
	);

	[[nodiscard]] bool empty();

	// the top right button. adds nothing while the history is empty
	void render_button(ui::Container& container);

	// unfinished renders stay, finished ones fold away after a few seconds
	void render_panel(ui::Container& container, float delta_time);

	// backdrop, then the button, then the rows
	void draw_panel(ui::Container& container, ui::Container& button_container);
}
