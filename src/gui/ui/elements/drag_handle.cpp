#include "../ui.h"
#include "../../render/render.h"

#include "../keys.h"

constexpr float DRAG_HANDLE_ANIMATION_SPEED = 25.f;
constexpr float DRAG_HANDLE_HOVER_ANIMATION_SPEED = 80.f;
constexpr float DRAG_HANDLE_LIFT_ANIMATION_SPEED = 40.f;

constexpr int DOT_ROWS = 3;
constexpr int DOT_COLUMNS = 2;
constexpr float DOT_RADIUS = 1.2f;
constexpr int DOT_GAP_X = 4;
constexpr int DOT_GAP_Y = 4;

constexpr float LIFT_ROUNDING = 6.f;
constexpr float LIFT_GROW = 2.f; // how much the card swells as it's picked up
const gfx::Color LIFT_FILL_COLOR(26, 26, 26, 255);
const gfx::Color LIFT_STROKE_COLOR = gfx::Color::white(55);

void ui::render_drag_handle(const Container& container, const AnimatedElement& element) {
	const auto& handle_data = std::get<DragHandleElementData>(element.element->data);

	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;
	float lift_anim = element.animations.at(hasher("lift")).current;

	if (lift_anim > 0.f && !handle_data.row_rect.is_empty()) {
		gfx::Rect row_rect = handle_data.row_rect;
		row_rect.y += element.element->rect.y - handle_data.row_anchor_y;

		gfx::Rect card = row_rect.expand(std::lround(std::lerp(0.f, LIFT_GROW, lift_anim)));

		float card_alpha = anim * lift_anim;

		render::rounded_rect_filled(card, LIFT_FILL_COLOR.adjust_alpha(card_alpha), LIFT_ROUNDING);
		render::rounded_rect_stroke(card, LIFT_STROKE_COLOR.adjust_alpha(card_alpha), LIFT_ROUNDING);
	}

	gfx::Color color = gfx::Color::white((uint8_t)std::lerp(90.f, 255.f, hover_anim)).adjust_alpha(anim);

	gfx::Point center = element.element->rect.center();

	float start_x = (float)center.x - ((float)(DOT_COLUMNS - 1) * DOT_GAP_X / 2.f);
	float start_y = (float)center.y - ((float)(DOT_ROWS - 1) * DOT_GAP_Y / 2.f);

	for (int row = 0; row < DOT_ROWS; row++) {
		for (int column = 0; column < DOT_COLUMNS; column++) {
			gfx::Point dot(
				std::lround(start_x + (float)(column * DOT_GAP_X)), std::lround(start_y + (float)(row * DOT_GAP_Y))
			);

			render::circle_filled(dot, DOT_RADIUS, color);
		}
	}
}

bool ui::update_drag_handle(const Container& container, AnimatedElement& element) {
	const auto& handle_data = std::get<DragHandleElementData>(element.element->data);
	auto& hover_anim = element.animations.at(hasher("hover"));

	bool hovered = element.element->rect.contains(keys::mouse_pos) && set_hovered_element(element);
	bool held = is_active_element(element, "drag handle");

	hover_anim.set_goal(hovered || held ? 1.f : 0.f);

	if (held) {
		set_cursor(SDL_SYSTEM_CURSOR_MOVE);
	}
	else if (hovered) {
		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

		if (!handle_data.tooltip.empty())
			tooltip::set(handle_data.tooltip);
	}

	if (hovered && !held && keys::is_mouse_down()) {
		set_active_element(element, "drag handle");
		keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

		keys::set_mouse_capture(true);

		return true;
	}

	if (held) {
		if (keys::is_mouse_dragging())
			return true;

		keys::set_mouse_capture(false);
		reset_active_element();
	}

	return false;
}

ui::AnimatedElement* ui::add_drag_handle(
	const std::string& id, Container& container, const gfx::Size& size, const std::string& tooltip
) {
	Element element(
		id,
		ElementType::DRAG_HANDLE,
		gfx::Rect(container.current_position, size),
		DragHandleElementData{
			.tooltip = tooltip,
		},
		render_drag_handle,
		update_drag_handle
	);

	return add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(DRAG_HANDLE_ANIMATION_SPEED) },
			{ hasher("hover"), AnimationState(DRAG_HANDLE_HOVER_ANIMATION_SPEED) },
			{ hasher("lift"), AnimationState(DRAG_HANDLE_LIFT_ANIMATION_SPEED) },
		}
	);
}
