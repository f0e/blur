#include "ui.h"
#include "keys.h"
#include "../render/render.h"

const gfx::Size TOOLTIP_PADDING(7, 4);
const gfx::Point TOOLTIP_CURSOR_OFFSET(13, 17);
const int TOOLTIP_SCREEN_PADDING = 6;
const float TOOLTIP_ROUNDING = 4.f;

namespace {
	// what's being hovered right now, filled in by elements as they update
	std::string requested_text;
	const render::Font* requested_font = nullptr;

	std::string hovered_id; // element the pending tooltip belongs to, empty if nothing's hovered
	std::string hovered_text;
	const render::Font* hovered_font = nullptr;
	std::chrono::steady_clock::time_point hover_start;

	// what's actually being drawn (kept around while it fades back out)
	std::string shown_text;
	const render::Font* shown_font = nullptr;
	gfx::Point shown_pos;

	ui::AnimationState anim(20.f);
}

void ui::tooltip::set(const std::string& text, const render::Font& font) {
	requested_text = text;
	requested_font = &font;
}

void ui::tooltip::on_input_end(const std::string& hovered_element_id) {
	// the tooltip belongs to whichever element won the hover, and only if it asked for one
	std::string new_id = requested_text.empty() ? "" : hovered_element_id;

	if (new_id != hovered_id) {
		hovered_id = new_id;
		hover_start = std::chrono::steady_clock::now();
	}

	hovered_text = requested_text;
	hovered_font = requested_font;

	requested_text.clear();
	requested_font = nullptr;
}

bool ui::tooltip::update(float delta_time) {
	bool waiting = !hovered_id.empty();

	bool show =
		waiting && std::chrono::duration<float>(std::chrono::steady_clock::now() - hover_start).count() >= DELAY;

	if (show && anim.goal == 0.f) {
		// just appeared, stick it where the cursor is now
		shown_text = hovered_text;
		shown_font = hovered_font;
		shown_pos = keys::mouse_pos;
	}

	anim.set_goal(show ? 1.f : 0.f);

	bool updated = anim.update(delta_time);

	// keep ticking while waiting on the delay, nothing else will wake us up to show it
	if (waiting && !show)
		updated = true;

	return updated;
}

void ui::tooltip::render() {
	if (anim.current < 0.01f || shown_text.empty() || !shown_font)
		return;

	gfx::Size text_size = shown_font->calc_size(shown_text);

	gfx::Rect rect(
		shown_pos.x + TOOLTIP_CURSOR_OFFSET.x,
		shown_pos.y + TOOLTIP_CURSOR_OFFSET.y,
		text_size.w + (TOOLTIP_PADDING.w * 2),
		text_size.h + (TOOLTIP_PADDING.h * 2)
	);

	// flip to the other side of the cursor if it would run off the screen
	if (rect.x2() > render::window_size.w - TOOLTIP_SCREEN_PADDING)
		rect.x = shown_pos.x - TOOLTIP_CURSOR_OFFSET.x - rect.w;

	if (rect.y2() > render::window_size.h - TOOLTIP_SCREEN_PADDING)
		rect.y = shown_pos.y - TOOLTIP_CURSOR_OFFSET.y - rect.h;

	rect.x = std::max(rect.x, TOOLTIP_SCREEN_PADDING);
	rect.y = std::max(rect.y, TOOLTIP_SCREEN_PADDING);

	gfx::Color background_color(7, 7, 7, anim.current * 255);
	gfx::Color border_color(70, 70, 70, anim.current * 255);
	gfx::Color text_color = gfx::Color::white(anim.current * 255);

	// draw above everything else
	render::late_draw_calls.emplace_back(
		[rect, background_color, border_color, text_color, text = shown_text, font = shown_font] {
			render::rounded_rect_filled(rect, background_color, TOOLTIP_ROUNDING);
			render::rounded_rect_stroke(rect, border_color, TOOLTIP_ROUNDING);

			render::text(rect.center(), text_color, text, *font, FONT_CENTERED_X | FONT_CENTERED_Y);
		}
	);
}
