#pragma once

#include "common/rendering.h"
#include "ui/ui.h"

namespace gui::renderer {
	inline constexpr uint8_t MUTED_SHADE = 155;

	inline constexpr int PAD_X = 24;
	inline constexpr int PAD_Y = PAD_X;

	inline constexpr float FPS_SMOOTHING = 0.95f;

	enum class Screens : uint8_t {
		TEST,
		MAIN,
		CONFIG
	};

	inline Screens screen = Screens::MAIN;

	inline ui::Container main_container;
	inline ui::Container config_container;
	inline ui::Container config_preview_header_container;
	inline ui::Container config_preview_content_container;
	inline ui::Container option_information_container;
	inline ui::Container notification_container;
	inline ui::Container nav_container;

	bool redraw_window(bool rendered_last, bool want_to_render);

	void on_render_finished(
		const rendering::VideoRenderDetails& render,
		const tl::expected<rendering::RenderResult, std::variant<std::string, rendering::RenderError>>& result
	);
}
