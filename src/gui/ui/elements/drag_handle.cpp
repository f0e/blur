#include "../ui.h"
#include "../../render/render.h"

#include "../keys.h"

constexpr float DRAG_HANDLE_ANIMATION_SPEED = 25.f;
constexpr float DRAG_HANDLE_HOVER_ANIMATION_SPEED = 80.f;

constexpr int DOT_ROWS = 3;
constexpr int DOT_COLUMNS = 2;
constexpr float DOT_RADIUS = 1.2f;
constexpr int DOT_GAP_X = 4;
constexpr int DOT_GAP_Y = 4;

void ui::render_drag_handle(const Container& container, const AnimatedElement& element) {
	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;

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
	auto& handle_data = std::get<DragHandleElementData>(element.element->data);
	auto& hover_anim = element.animations.at(hasher("hover"));

	handle_data.pressed = false;

	bool hovered = element.element->rect.contains(keys::mouse_pos) && set_hovered_element(element);
	bool active = get_active_element() == &element;

	hover_anim.set_goal(hovered || active ? 1.f : 0.f);

	if (hovered || active)
		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

	if (hovered && !active && keys::is_mouse_down()) {
		set_active_element(element, "drag handle");
		keys::on_mouse_press_handled(SDL_BUTTON_LEFT);
		handle_data.pressed = true;

		return true;
	}

	// the list moves rows around under the cursor while a drag is running, so the handle only reports
	// the grab and lets go on release - it's the list that knows which row is being dragged
	if (active) {
		if (keys::is_mouse_dragging())
			return true;

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
		}
	);
}
