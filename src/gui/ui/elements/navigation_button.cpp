#include "../ui.h"
#include "../../render/render.h"

#include "../keys.h"

namespace {
	const std::string NAVIGATION_BACK_ICON = "k";
	const std::string NAVIGATION_SETTINGS_ICON = "l";

	constexpr float NAVIGATION_BUTTON_ICON_SIZE = 18.f;
	constexpr float NAVIGATION_BUTTON_ROUNDING = 10.f;
	constexpr float NAVIGATION_BUTTON_TAB_ROUNDING = 5.f;
	constexpr int NAVIGATION_BUTTON_LABEL_GAP = 5;
	constexpr int NAVIGATION_BUTTON_LABEL_PADDING_X = 11;
	constexpr int NAVIGATION_BUTTON_LABEL_PADDING_Y = 11;

	constexpr int NAVIGATION_BUTTON_SHADE = 0;
	constexpr int NAVIGATION_BUTTON_HOVER_SHADE = 28;
	constexpr int NAVIGATION_BUTTON_STROKE_SHADE = 38;
	constexpr int NAVIGATION_BUTTON_STROKE_HOVER_SHADE = 90;
	constexpr int NAVIGATION_BUTTON_ICON_SHADE = 140;
	constexpr int NAVIGATION_BUTTON_ICON_HOVER_SHADE = 255;

	constexpr float NAVIGATION_BUTTON_ANIMATION_SPEED = 25.f;
	constexpr float NAVIGATION_BUTTON_HOVER_ANIMATION_SPEED = 80.f;

	const std::string& navigation_icon_text(ui::NavigationIcon icon) {
		switch (icon) {
			case ui::NavigationIcon::BACK:
				return NAVIGATION_BACK_ICON;
			case ui::NavigationIcon::SETTINGS:
				return NAVIGATION_SETTINGS_ICON;
		}

		return NAVIGATION_SETTINGS_ICON;
	}
}

void ui::render_navigation_button(const Container& container, const AnimatedElement& element) {
	const auto& button_data = std::get<NavigationButtonElementData>(element.element->data);
	const float anim = element.animations.at(hasher("main")).current;
	const float hover_anim = element.animations.at(hasher("hover")).current;

	const int background_shade =
		u::lerp((float)NAVIGATION_BUTTON_SHADE, (float)NAVIGATION_BUTTON_HOVER_SHADE, hover_anim);
	const int border_shade =
		u::lerp((float)NAVIGATION_BUTTON_STROKE_SHADE, (float)NAVIGATION_BUTTON_STROKE_HOVER_SHADE, hover_anim);
	const int icon_shade =
		u::lerp((float)NAVIGATION_BUTTON_ICON_SHADE, (float)NAVIGATION_BUTTON_ICON_HOVER_SHADE, hover_anim);
	const float rounding =
		button_data.style == NavigationButtonStyle::TAB ? NAVIGATION_BUTTON_TAB_ROUNDING : NAVIGATION_BUTTON_ROUNDING;

	const auto background_color = gfx::Color(background_shade, background_shade, background_shade, anim * 255);
	const auto border_color = gfx::Color::white(border_shade * anim);
	const auto icon_color = gfx::Color::white(icon_shade * anim);

	render::rounded_rect_filled(element.element->rect, background_color, rounding);
	render::rounded_rect_stroke(element.element->rect, border_color, rounding);

	const auto& icon_font = fonts::icons(NAVIGATION_BUTTON_ICON_SIZE);
	const auto label_size = fonts::dejavu.calc_size(button_data.label);
	const std::string* icon = button_data.icon ? &navigation_icon_text(*button_data.icon) : nullptr;
	const gfx::Size icon_size = icon ? icon_font.calc_size(*icon) : gfx::Size{};

	if (button_data.label.empty()) {
		if (icon) {
			render::text(
				element.element->rect.center(), icon_color, *icon, icon_font, FONT_CENTERED_X | FONT_CENTERED_Y
			);
		}
		return;
	}

	const int icon_and_gap_width = icon ? icon_size.w + NAVIGATION_BUTTON_LABEL_GAP : 0;
	const int content_width = icon_and_gap_width + label_size.w;
	const int content_x = element.element->rect.center().x - content_width / 2;

	if (icon) {
		render::text(
			gfx::Point(content_x + icon_size.w / 2, element.element->rect.center().y),
			icon_color,
			*icon,
			icon_font,
			FONT_CENTERED_X | FONT_CENTERED_Y
		);
	}
	render::text(
		gfx::Point(content_x + icon_and_gap_width, element.element->rect.center().y),
		gfx::Color::white(255 * anim),
		button_data.label,
		fonts::dejavu,
		FONT_CENTERED_Y
	);
}

bool ui::update_navigation_button(const Container& container, AnimatedElement& element) {
	const auto& button_data = std::get<NavigationButtonElementData>(element.element->data);
	auto& hover_anim = element.animations.at(hasher("hover"));

	const bool hovered = element.element->rect.contains(keys::mouse_pos) && set_hovered_element(element);
	hover_anim.set_goal(hovered ? 1.f : 0.f);

	if (!hovered)
		return false;

	set_cursor(SDL_SYSTEM_CURSOR_POINTER);

	if (!button_data.tooltip.empty())
		tooltip::set(button_data.tooltip);

	if (button_data.on_press && keys::is_mouse_down()) {
		(*button_data.on_press)();
		keys::on_mouse_press_handled(SDL_BUTTON_LEFT);
		return true;
	}

	return false;
}

ui::AnimatedElement* ui::add_navigation_button(
	const std::string& id,
	Container& container,
	std::optional<NavigationIcon> icon,
	std::optional<std::function<void()>> on_press,
	const std::string& tooltip,
	NavigationButtonStyle style,
	const std::string& label
) {
	gfx::Size button_size = container.get_usable_rect().size();
	if (!label.empty()) {
		const auto label_size = fonts::dejavu.calc_size(label);
		gfx::Size icon_size;
		if (icon)
			icon_size = fonts::icons(NAVIGATION_BUTTON_ICON_SIZE).calc_size(navigation_icon_text(*icon));

		const int icon_and_gap_width = icon ? icon_size.w + NAVIGATION_BUTTON_LABEL_GAP : 0;
		button_size = gfx::Size(
			NAVIGATION_BUTTON_LABEL_PADDING_X * 2 + icon_and_gap_width + label_size.w,
			label_size.h + NAVIGATION_BUTTON_LABEL_PADDING_Y * 2
		);
	}

	Element element(
		id,
		ElementType::NAVIGATION_BUTTON,
		gfx::Rect(container.current_position, button_size),
		NavigationButtonElementData{
			.icon = icon,
			.style = style,
			.label = label,
			.tooltip = tooltip,
			.on_press = std::move(on_press),
		},
		render_navigation_button,
		update_navigation_button
	);

	return add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(NAVIGATION_BUTTON_ANIMATION_SPEED) },
			{ hasher("hover"), AnimationState(NAVIGATION_BUTTON_HOVER_ANIMATION_SPEED) },
		}
	);
}

ui::AnimatedElement* ui::add_navigation_button(
	const std::string& id,
	Container& container,
	const std::string& label,
	std::optional<std::function<void()>> on_press,
	const std::string& tooltip,
	NavigationButtonStyle style
) {
	return add_navigation_button(id, container, std::nullopt, std::move(on_press), tooltip, style, label);
}
