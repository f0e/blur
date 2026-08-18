#include "../ui.h"
#include "../../render/render.h"
#include "../keys.h"

const float CHECKBOX_ROUNDING = 1.0f;
const int CHECKBOX_SIZE = 7;
const int LABEL_GAP = 5;

void ui::render_checkbox(const Container& container, const AnimatedElement& element) {
	const auto& checkbox_data = std::get<CheckboxElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;
	float check_anim = element.animations.at(hasher("check")).current;

	// render::rect_filled(element.element->rect, gfx::Color(255, 0, 0, 100));

	// Checkbox background
	int shade = 50 + (50 * hover_anim);
	gfx::Color bg_color = gfx::Color(shade, shade, shade, anim * 255);

	// Checkbox border and check mark colors
	gfx::Color border_color = gfx::Color(100, 100, 100, anim * 255);
	gfx::Color check_color = highlight_color.adjust_alpha(anim * check_anim);

	// render::rect_stroke(element.element->rect, gfx::Color(255, 0, 0, 255));

	// Calculate checkbox rect (centered vertically with text)
	gfx::Rect checkbox_rect = element.element->rect;
	checkbox_rect.w = CHECKBOX_SIZE;
	checkbox_rect.h = CHECKBOX_SIZE;
	checkbox_rect.y += (element.element->rect.h - CHECKBOX_SIZE) / 2;

	// checkbox
	render::rect_filled(checkbox_rect, bg_color);
	render::rect_stroke(checkbox_rect, border_color);

	// checked
	render::rect_filled(checkbox_rect, check_color);

	// Render label text
	gfx::Point text_pos = element.element->rect.origin();
	text_pos.x += CHECKBOX_SIZE + LABEL_GAP;
	text_pos.y = element.element->rect.center().y;

	render::text(text_pos, gfx::Color::white(anim * 255), checkbox_data.label, checkbox_data.font, FONT_CENTERED_Y);
}

bool ui::update_checkbox(const Container& container, AnimatedElement& element) {
	const auto& checkbox_data = std::get<CheckboxElementData>(element.element->data);

	auto& hover_anim = element.animations.at(hasher("hover"));
	auto& check_anim = element.animations.at(hasher("check"));

	bool hovered = element.element->rect.contains(keys::mouse_pos) && set_hovered_element(element);
	hover_anim.set_goal(hovered ? 1.f : 0.f);
	check_anim.set_goal(*checkbox_data.checked ? 1.f : 0.f);

	if (hovered) {
		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

		if (keys::is_mouse_down()) {
			*checkbox_data.checked = !(*checkbox_data.checked);
			check_anim.set_goal(*checkbox_data.checked ? 1.f : 0.f);

			keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

			if (checkbox_data.on_change)
				(*checkbox_data.on_change)(*checkbox_data.checked);

			return true;
		}
	}

	return false;
}

ui::AnimatedElement* ui::add_checkbox(
	const std::string& id,
	Container& container,
	const std::string& label,
	bool& checked,
	const render::Font& font,
	std::optional<std::function<void(bool)>> on_change
) {
	gfx::Size total_size(200, std::max(CHECKBOX_SIZE, font.height()));

	Element element(
		id,
		ElementType::CHECKBOX,
		gfx::Rect(container.current_position, total_size),
		CheckboxElementData{
			.label = label,
			.checked = &checked,
			.font = font,
			.on_change = std::move(on_change),
		},
		render_checkbox,
		update_checkbox
	);

	return add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(25.f) },
			{ hasher("hover"), AnimationState(80.f) },
			{ hasher("check"), AnimationState(40.f, checked ? 1.f : 0.f) },
		}
	);
}
