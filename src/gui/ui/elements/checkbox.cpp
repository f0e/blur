#include <utility>

#include "../ui.h"
#include "../render.h"
#include "../utils.h"
#include "../keys.h"

#include "../../renderer.h"

const float checkbox_rounding = 1.0f;
const float checkbox_size = 7.0f;
const int label_gap = 5;

void ui::render_checkbox(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	const auto& checkbox_data = std::get<CheckboxElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;
	float check_anim = element.animations.at(hasher("check")).current;

	// render::rect_filled(surface, element.element->rect, gfx::rgba(255, 0, 0, 100));

	// Checkbox background
	int shade = 50 + (50 * hover_anim);
	gfx::Color bg_color = utils::adjust_color(gfx::rgba(shade, shade, shade, 255), anim);

	// Checkbox border and check mark colors
	gfx::Color border_color = utils::adjust_color(gfx::rgba(100, 100, 100, 255), anim);
	gfx::Color check_color = utils::adjust_color(HIGHLIGHT_COLOR, anim * check_anim);

	// render::rect_stroke(surface, element.element->rect, gfx::rgba(255, 0, 0, 255));

	// Calculate checkbox rect (centered vertically with text)
	gfx::Rect checkbox_rect = element.element->rect;
	checkbox_rect.w = checkbox_size;
	checkbox_rect.h = checkbox_size;
	checkbox_rect.y += (element.element->rect.h - checkbox_size) / 2;

	// checkbox
	render::rect_filled(surface, checkbox_rect, bg_color);
	render::rect_stroke(surface, checkbox_rect, border_color);

	// checked
	render::rect_filled(surface, checkbox_rect, check_color);

	// Render label text
	gfx::Point text_pos = element.element->rect.origin();
	text_pos.x += checkbox_size + label_gap;
	text_pos.y = element.element->rect.center().y;

	// vertically center todo: use get_text_size in other components now that its accurate
	float text_size = render::get_text_size(checkbox_data.label, checkbox_data.font).h;
	text_pos.y += text_size / 2;

	// gfx::Rect text_debug_rect(text_pos, gfx::Size(element.element->rect.w, text_size));
	// text_debug_rect.y -= text_debug_rect.h;
	// render::rect_stroke(surface, text_debug_rect, gfx::rgba(0, 255, 0, 255));

	render::text(
		surface,
		text_pos,
		utils::adjust_color(gfx::rgba(255, 255, 255, 255), anim),
		checkbox_data.label,
		checkbox_data.font,
		os::TextAlign::Left
	);
}

bool ui::update_checkbox(const Container& container, AnimatedElement& element) {
	const auto& checkbox_data = std::get<CheckboxElementData>(element.element->data);

	auto& hover_anim = element.animations.at(hasher("hover"));
	auto& check_anim = element.animations.at(hasher("check"));

	bool hovered = element.element->rect.contains(keys::mouse_pos);
	hover_anim.set_goal(hovered ? 1.f : 0.f);

	if (hovered) {
		set_cursor(os::NativeCursor::Link);

		if (keys::is_mouse_down()) {
			*checkbox_data.checked = !(*checkbox_data.checked);
			check_anim.set_goal(*checkbox_data.checked ? 1.f : 0.f);

			keys::on_mouse_press_handled(os::Event::MouseButton::LeftButton);

			if (checkbox_data.on_change)
				(*checkbox_data.on_change)(*checkbox_data.checked);

			return true;
		}
	}

	return false;
}

ui::Element& ui::add_checkbox(
	const std::string& id,
	Container& container,
	const std::string& label,
	bool& checked,
	const SkFont& font,
	std::optional<std::function<void(bool)>> on_change
) {
	gfx::Size text_size = render::get_text_size(label, font);
	gfx::Size total_size(200, std::max(checkbox_size, font.getSize()));

	Element element = {
		.type = ElementType::CHECKBOX,
		.rect = gfx::Rect(container.current_position, total_size),
		.data =
			CheckboxElementData{
				.label = label,
				.checked = &checked,
				.font = font,
				.on_change = std::move(on_change),
			},
		.render_fn = render_checkbox,
		.update_fn = update_checkbox,
	};

	return *add_element(
		container,
		id,
		std::move(element),
		container.line_height,
		{
			{ hasher("main"), { .speed = 25.f } },
			{ hasher("hover"), { .speed = 80.f } },
			{
				hasher("check"),
				{
					.speed = 40.f,
					.value = checked ? 1.f : 0.f,
				},
			},
		}
	);
}
