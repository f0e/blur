#include "../ui.h"
#include "../../render/render.h"

#include "../keys.h"

const gfx::Size TAB_PADDING = { 10, 7 };
const float TAB_ROUNDING = 5.f;

namespace {
	void update_background(ui::AnimatedElement& element) {
		const auto& tabs_data = std::get<ui::TabsElementData>(element.element->data);

		auto& selected_offset_anim = element.animations.at(ui::hasher("background_offset"));
		auto& selected_width_anim = element.animations.at(ui::hasher("background_width"));

		for (size_t i = 0; i < tabs_data.options.size(); i++) {
			if (tabs_data.options[i] == *tabs_data.selected) {
				selected_offset_anim.set_goal(tabs_data.option_offset_rects[i].x);
				selected_width_anim.set_goal(tabs_data.option_offset_rects[i].w);
				break;
			}
		}
	}
}

void ui::render_tabs(const Container& container, const AnimatedElement& element) {
	const auto& tabs_data = std::get<TabsElementData>(element.element->data);

	float anim = element.animations.at(hasher("main")).current;

	float selected_offset = element.animations.at(hasher("background_offset")).current;
	float selected_width = element.animations.at(hasher("background_width")).current;

	render::rounded_rect_filled(element.element->rect, gfx::Color::black(anim * 255), 5);

	for (size_t i = 0; i < tabs_data.options.size(); i++) {
		const auto& option = tabs_data.options[i];
		const auto& option_rect = tabs_data.option_offset_rects[i] + element.element->rect.origin();

		render::text(
			option_rect.center(),
			gfx::Color(100, 100, 100, anim * 255),
			option,
			*tabs_data.font,
			FONT_CENTERED_X | FONT_CENTERED_Y
		);
	}

	auto background_rect =
		gfx::Rect(selected_offset, 0, selected_width, element.element->rect.h) + element.element->rect.origin();

	render::rounded_rect_filled(background_rect, gfx::Color::white(anim * 255), TAB_ROUNDING);

	render::push_clip_rect(background_rect);

	for (size_t i = 0; i < tabs_data.options.size(); i++) {
		const auto& option = tabs_data.options[i];
		const auto& option_rect = tabs_data.option_offset_rects[i] + element.element->rect.origin();

		render::text(
			option_rect.center(),
			gfx::Color::black(anim * 255),
			option,
			*tabs_data.font,
			FONT_CENTERED_X | FONT_CENTERED_Y
		);
	}

	render::pop_clip_rect();

	render::rounded_rect_stroke(element.element->rect, gfx::Color(100, 100, 100, anim * 255), TAB_ROUNDING);
}

bool ui::update_tabs(const Container& container, AnimatedElement& element) {
	const auto& tabs_data = std::get<TabsElementData>(element.element->data);

	bool hovered = element.element->rect.contains(keys::mouse_pos) && set_hovered_element(element);

	if (hovered) {
		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

		if (keys::is_mouse_down()) {
			for (size_t i = 0; i < tabs_data.options.size(); i++) {
				auto option_rect = tabs_data.option_offset_rects[i] + element.element->rect.origin();

				if (option_rect.contains(keys::mouse_pos)) {
					*tabs_data.selected = tabs_data.options[i];

					update_background(element);

					if (tabs_data.on_select)
						(*tabs_data.on_select)();

					keys::on_mouse_press_handled(SDL_BUTTON_LEFT);
					return true;
				}
			}
		}
	}

	update_background(element);

	return false;
}

int ui::tabs_height(const render::Font& font) {
	return font.height() + (TAB_PADDING.h * 2);
}

ui::AnimatedElement* ui::add_tabs(
	const std::string& id,
	Container& container,
	const std::vector<std::string>& options,
	std::string& selected,
	const render::Font& font,
	std::optional<std::function<void()>> on_select
) {
	gfx::Rect rect(container.current_position, gfx::Size(0, tabs_height(font)));
	std::vector<gfx::Rect> option_offset_rects;

	gfx::Rect selected_offset_rect;

	for (size_t i = 0; i < options.size(); i++) {
		const auto& option = options[i];

		gfx::Size text_size = font.calc_size(option);

		gfx::Rect option_rect = { 0, 0, text_size.w + (TAB_PADDING.w * 2), rect.h };
		if (i > 0)
			option_rect.x = option_offset_rects.back().x2();

		option_offset_rects.push_back(option_rect);

		if (option == selected)
			selected_offset_rect = option_rect;

		rect.w += option_rect.w;
	}

	Element element(
		id,
		ElementType::TABS,
		rect,
		TabsElementData{
			.options = options,
			.selected = &selected,
			.font = &font,
			.on_select = std::move(on_select),
			.option_offset_rects = std::move(option_offset_rects),
		},
		render_tabs,
		update_tabs
	);

	auto* animated_element = add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(25.f) },
			{ hasher("background_offset"), AnimationState(25.f, selected_offset_rect.x) },
			{ hasher("background_width"), AnimationState(25.f, selected_offset_rect.w) },
		}
	);

	update_background(*animated_element);

	return animated_element;
}
