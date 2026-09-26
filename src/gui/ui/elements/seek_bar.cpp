#include "../ui.h"
#include "../../render/render.h"
#include "../keys.h"

constexpr float TRACK_ROUNDING = 3.f;
constexpr int PLAYHEAD_WIDTH = 2;
constexpr gfx::Size PADDING(6, 4);

namespace {
	std::string format_time(float seconds) {
		auto total_seconds = static_cast<int>(std::max(seconds, 0.f));

		int hours = total_seconds / 3600;
		int minutes = (total_seconds % 3600) / 60;
		int secs = total_seconds % 60;

		if (hours > 0)
			return std::format("{}:{:02}:{:02}", hours, minutes, secs);

		return std::format("{}:{:02}", minutes, secs);
	}
}

int ui::seek_bar_height(const render::Font& font) {
	return font.height() + (PADDING.h * 2);
}

void ui::render_seek_bar(const Container& container, const AnimatedElement& element) {
	const auto& seek_bar_data = std::get<SeekBarElementData>(element.element->data);

	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;

	float progress = std::clamp(*seek_bar_data.value, 0.f, 1.f);
	const auto& rect = element.element->rect;

	int track_shade = 15 + static_cast<int>(8 * hover_anim);
	render::rounded_rect_filled(rect, gfx::Color(track_shade, track_shade, track_shade, anim * 255), TRACK_ROUNDING);

	int stroke_shade = 45 + static_cast<int>(25 * hover_anim);
	render::rounded_rect_stroke(rect, gfx::Color(stroke_shade, stroke_shade, stroke_shade, anim * 255), TRACK_ROUNDING);

	gfx::Rect playhead_rect = rect;
	playhead_rect.w = PLAYHEAD_WIDTH;
	playhead_rect.x += static_cast<int>((rect.w - PLAYHEAD_WIDTH) * progress);

	render::rounded_rect_filled(
		playhead_rect, gfx::Color::white(anim * (180 + (75 * hover_anim))), PLAYHEAD_WIDTH / 2.f
	);

	if (!seek_bar_data.font || seek_bar_data.duration <= 0.f)
		return;

	if (hover_anim > 0.f) {
		int text_y = rect.center().y;

		render::text(
			gfx::Point(rect.x + PADDING.w, text_y),
			gfx::Color::white(anim * hover_anim * 255),
			format_time(progress * seek_bar_data.duration),
			seek_bar_data.font,
			FONT_CENTERED_Y
		);

		render::text(
			gfx::Point(rect.x2() - PADDING.w, text_y),
			gfx::Color::white(anim * hover_anim * 100),
			format_time(seek_bar_data.duration),
			seek_bar_data.font,
			FONT_RIGHT_ALIGN | FONT_CENTERED_Y
		);
	}
}

bool ui::update_seek_bar(const Container& container, AnimatedElement& element) {
	auto& seek_bar_data = std::get<SeekBarElementData>(element.element->data);

	auto& hover_anim = element.animations.at(hasher("hover"));

	bool hovered = element.element->rect.contains(keys::mouse_pos) && set_hovered_element(element);
	bool active = get_active_element() == &element;

	hover_anim.set_goal(hovered || active ? 1.f : 0.f);

	if (hovered) {
		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

		if (keys::is_mouse_down())
			set_active_element(element, "seek bar");
	}

	if (get_active_element() == &element) {
		if (keys::is_mouse_down()) {
			float progress = element.element->rect.mouse_percent_x();
			*seek_bar_data.value = progress;

			return true;
		}

		reset_active_element();
	}

	return false;
}

ui::AnimatedElement* ui::add_seek_bar(
	const std::string& id,
	Container& container,
	float& value,
	const render::Font& font,
	float duration,
	std::optional<int> width
) {
	gfx::Size seek_bar_size(width.value_or(container.get_usable_rect().w), seek_bar_height(font));

	Element element(
		id,
		ElementType::SEEK_BAR,
		gfx::Rect(container.current_position, seek_bar_size),
		SeekBarElementData{
			.value = &value,
			.duration = duration,
			.font = font,
		},
		render_seek_bar,
		update_seek_bar
	);

	return add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(25.f) },
			{ hasher("hover"), AnimationState(80.f) },
		}
	);
}
