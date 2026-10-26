#include "ui.h"
#include "keys.h"
#include "../render/render.h"

const int CONTEXT_MENU_PADDING = 4;
const gfx::Size CONTEXT_MENU_ITEM_PADDING(10, 5);
const int CONTEXT_MENU_ICON_GAP = 8;
const int CONTEXT_MENU_SCREEN_PADDING = 6;
const float CONTEXT_MENU_ROUNDING = 6.f;
const float CONTEXT_MENU_ITEM_ROUNDING = 4.f;
const float CONTEXT_MENU_FADED_OUT = 0.01f;

namespace {
	// kept after closing so it can fade out
	std::vector<ui::ContextMenuItem> items;
	bool shown = false;

	gfx::Rect menu_rect;
	int icon_width = 0;
	int hovered = -1;

	ui::AnimationState anim(25.f);

	int item_height() {
		return fonts::dejavu.height() + (CONTEXT_MENU_ITEM_PADDING.h * 2);
	}

	gfx::Rect item_rect(size_t index) {
		return {
			menu_rect.x + CONTEXT_MENU_PADDING,
			menu_rect.y + CONTEXT_MENU_PADDING + (static_cast<int>(index) * item_height()),
			menu_rect.w - (CONTEXT_MENU_PADDING * 2),
			item_height(),
		};
	}

	int get_hovered_item() {
		for (size_t i = 0; i < items.size(); i++) {
			if (item_rect(i).contains(keys::mouse_pos))
				return static_cast<int>(i);
		}

		return -1;
	}
}

void ui::context_menu::open(std::vector<ContextMenuItem> new_items) {
	items = std::move(new_items);
	shown = true;
	hovered = -1;

	icon_width = 0;
	int label_width = 0;
	for (const auto& item : items) {
		if (item.icon)
			icon_width = std::max(icon_width, fonts::icons.calc_size(*item.icon).w);

		label_width = std::max(label_width, fonts::dejavu.calc_size(item.label).w);
	}

	int content_width = label_width + (icon_width > 0 ? icon_width + CONTEXT_MENU_ICON_GAP : 0);

	menu_rect = gfx::Rect(
		keys::mouse_pos.x,
		keys::mouse_pos.y,
		content_width + ((CONTEXT_MENU_PADDING + CONTEXT_MENU_ITEM_PADDING.w) * 2),
		(static_cast<int>(items.size()) * item_height()) + (CONTEXT_MENU_PADDING * 2)
	);

	// open up or left of the cursor if it would run off the screen
	if (menu_rect.x2() > render::window_size.w - CONTEXT_MENU_SCREEN_PADDING)
		menu_rect.x -= menu_rect.w;

	if (menu_rect.y2() > render::window_size.h - CONTEXT_MENU_SCREEN_PADDING)
		menu_rect.y -= menu_rect.h;

	menu_rect.x = std::max(menu_rect.x, CONTEXT_MENU_SCREEN_PADDING);
	menu_rect.y = std::max(menu_rect.y, CONTEXT_MENU_SCREEN_PADDING);
}

void ui::context_menu::close() {
	shown = false;
	hovered = -1;
}

bool ui::context_menu::is_open() {
	return shown;
}

void ui::context_menu::build() {
	// handled before the screens are built so they don't get escape too
	if (shown && keys::is_key_pressed(SDL_SCANCODE_ESCAPE)) {
		keys::on_key_press_handled(SDL_SCANCODE_ESCAPE);
		close();
	}
}

bool ui::context_menu::update_input() {
	if (!shown)
		return false;

	int new_hovered = get_hovered_item();
	bool updated = new_hovered != hovered;
	hovered = new_hovered;

	if (keys::is_mouse_down()) {
		keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

		// copied before closing, the callback might open another menu
		std::function<void()> on_press;
		if (hovered >= 0)
			on_press = items[hovered].on_press;

		close();

		if (on_press)
			on_press();

		return true;
	}

	if (keys::is_mouse_down(SDL_BUTTON_RIGHT)) {
		// left unhandled outside the menu so it can open another one where it was clicked
		if (menu_rect.contains(keys::mouse_pos))
			keys::on_mouse_press_handled(SDL_BUTTON_RIGHT);

		close();
		return true;
	}

	return updated;
}

bool ui::context_menu::update_frame(float delta_time) {
	anim.set_goal(shown ? 1.f : 0.f);
	bool updated = anim.update(delta_time);

	// their callbacks can hold on to things like video players
	if (!shown && anim.current < CONTEXT_MENU_FADED_OUT)
		items.clear();

	return updated;
}

void ui::context_menu::render() {
	if (anim.current < CONTEXT_MENU_FADED_OUT)
		return;

	gfx::Color background_color(7, 7, 7, anim.current * 255);
	gfx::Color border_color(70, 70, 70, anim.current * 255);
	gfx::Color hover_color = gfx::Color::white(anim.current * 25);
	gfx::Color text_color = gfx::Color::white(anim.current * 255);

	// draw above everything else, under the tooltip. nothing changes the menu before it's drawn this frame
	render::late_draw_calls.emplace_back([background_color, border_color, hover_color, text_color] {
		render::rounded_rect_filled(menu_rect, background_color, CONTEXT_MENU_ROUNDING);
		render::rounded_rect_stroke(menu_rect, border_color, CONTEXT_MENU_ROUNDING);

		for (auto [i, item] : u::enumerate(items)) {
			auto row_rect = item_rect(i);

			if (static_cast<int>(i) == hovered)
				render::rounded_rect_filled(row_rect, hover_color, CONTEXT_MENU_ITEM_ROUNDING);

			int x = row_rect.x + CONTEXT_MENU_ITEM_PADDING.w;

			if (icon_width > 0) {
				if (item.icon) {
					render::text(
						gfx::Point(x + (icon_width / 2), row_rect.center().y),
						text_color,
						*item.icon,
						fonts::icons,
						FONT_CENTERED_X | FONT_CENTERED_Y
					);
				}

				x += icon_width + CONTEXT_MENU_ICON_GAP;
			}

			render::text(gfx::Point(x, row_rect.center().y), text_color, item.label, fonts::dejavu, FONT_CENTERED_Y);
		}
	});
}
