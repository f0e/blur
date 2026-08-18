#include "../ui.h"
#include "../../render/render.h"
#include "../keys.h"

static const int LINK_HIT_PAD = 4;

void ui::render_link(const Container& container, const AnimatedElement& element) {
	const auto& link_data = std::get<LinkElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;
	float hover = element.animations.at(hasher("hover")).current;

	gfx::Color color = gfx::Color::lerp(link_data.color, link_data.hover_color, hover).adjust_alpha(anim);

	render::text(element.element->rect.top_left(), color, link_data.text, link_data.font);

	// underline fades in on hover so it reads as clickable
	if (hover > 0.f) {
		gfx::Rect underline(element.element->rect.x, element.element->rect.y2() + 1, element.element->rect.w, 1);
		render::rect_filled(underline, color.adjust_alpha(hover * 0.6f));
	}
}

bool ui::update_link(const Container& container, AnimatedElement& element) {
	const auto& link_data = std::get<LinkElementData>(element.element->data);

	auto& anim = element.animations.at(hasher("hover"));

	bool hovered = element.element->rect.expand(LINK_HIT_PAD).contains(keys::mouse_pos) && set_hovered_element(element);
	anim.set_goal(hovered ? 1.f : 0.f);

	if (!hovered || !link_data.on_press)
		return false;

	set_cursor(SDL_SYSTEM_CURSOR_POINTER);

	if (!keys::is_mouse_down())
		return false;

	(*link_data.on_press)();
	keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

	return true;
}

ui::AnimatedElement* ui::add_link(
	const std::string& id,
	Container& container,
	const std::string& text,
	const render::Font& font,
	std::optional<std::function<void()>> on_press,
	gfx::Color color,
	gfx::Color hover_color
) {
	gfx::Size size = font.calc_size(text);

	Element element(
		id,
		ElementType::LINK,
		gfx::Rect(container.current_position, size),
		LinkElementData{
			.text = text,
			.on_press = std::move(on_press),
			.color = color,
			.hover_color = hover_color,
			.font = font,
		},
		render_link,
		update_link
	);

	return add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(15.f) },
			{ hasher("hover"), AnimationState(80.f) },
		}
	);
}
