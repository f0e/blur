#pragma once

#include "../ui/ui.h"
#include "common/rendering.h"

namespace gui::components::render_history {
	inline constexpr int BUTTON_SIZE = 24;

	// renders finished this session, newest first. not persisted anywhere
	void add_success(const rendering::RenderResult& result);
	void add_failure(
		const rendering::VideoRenderDetails& render, const std::variant<std::string, rendering::RenderError>& error
	);

	[[nodiscard]] bool empty();

	// the top right button. adds nothing while the history is empty
	void render_button(ui::Container& container);

	// the panel the button opens on hover. new renders show up in it for a few seconds on their own, then fold away
	// into the button.
	// with_button is for screens that don't have the button (see render_button) - there's nothing to hover and
	// nowhere for the rows to fold into, so they just fade out
	void render_panel(ui::Container& container, float delta_time, bool with_button);

	// draws the panel: its backdrop, then the button, then the rows clipped inside it. the button sits above the
	// backdrop but below the rows, so scrolling entries cover it back up
	void draw_panel(ui::Container& container, ui::Container& button_container);
}
