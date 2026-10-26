#include "../ui.h"
#include "../../render/render.h"
#include "../../fonts/icons.h"

#include "../keys.h"

constexpr float INFO_ICON_ANIMATION_SPEED = 25.f;
constexpr float INFO_ICON_HOVER_ANIMATION_SPEED = 40.f;

constexpr float POPOVER_ROUNDING = 5.f;
constexpr gfx::Size POPOVER_PADDING(10, 8);
constexpr int POPOVER_GAP = 6;
constexpr int POPOVER_SLIDE = 4;
constexpr int POPOVER_SCREEN_PADDING = 6;
constexpr int TITLE_GAP = 7;
constexpr int ROW_GAP = 4;
constexpr int COLUMN_GAP = 14;

namespace {
	struct PopoverLayout {
		gfx::Size size;
		int value_x{};
		int row_height{};
	};

	PopoverLayout get_popover_layout(
		const std::string& title, const std::vector<ui::InfoRow>& rows, const render::Font& font
	) {
		PopoverLayout layout;
		layout.row_height = font.height() + ROW_GAP;

		int label_width = 0;
		int value_width = 0;
		for (const auto& row : rows) {
			label_width = std::max(label_width, font.calc_size(row.label).w);
			value_width = std::max(value_width, font.calc_size(row.value).w);
		}

		layout.value_x = label_width + COLUMN_GAP;

		int width = std::max(font.calc_size(title).w, layout.value_x + value_width);
		int height = font.height() + (TITLE_GAP * 2) + (layout.row_height * static_cast<int>(rows.size())) - ROW_GAP;

		layout.size = gfx::Size(width, height) + POPOVER_PADDING * 2;
		return layout;
	}

	gfx::Rect get_popover_rect(const gfx::Rect& anchor, const gfx::Size& size, bool below) {
		// right edges lined up with the anchor, flipping sides if there's no room
		int above_y = anchor.y - POPOVER_GAP - size.h;
		int below_y = anchor.y2() + POPOVER_GAP;

		gfx::Rect rect(anchor.x2() - size.w, below ? below_y : above_y, size.w, size.h);

		if (below && rect.y2() > render::window_size.h - POPOVER_SCREEN_PADDING)
			rect.y = above_y;
		else if (!below && rect.y < POPOVER_SCREEN_PADDING)
			rect.y = below_y;

		rect.x = std::clamp(
			rect.x, POPOVER_SCREEN_PADDING, std::max(render::window_size.w - POPOVER_SCREEN_PADDING - rect.w, 0)
		);

		return rect;
	}
}

void ui::render_info_popover(
	const gfx::Rect& anchor,
	const std::string& title,
	const std::vector<InfoRow>& rows,
	const render::Font& font,
	float anim,
	bool below
) {
	if (anim < 0.01f)
		return;

	auto layout = get_popover_layout(title, rows, font);
	gfx::Rect rect = get_popover_rect(anchor, layout.size, below);
	rect.y += static_cast<int>((1.f - anim) * POPOVER_SLIDE);

	render::late_draw_calls.emplace_back([rect, layout, anim, title, rows, font] {
		gfx::Color background_color(7, 7, 7, anim * 255);
		gfx::Color border_color(70, 70, 70, anim * 255);
		gfx::Color separator_color = gfx::Color::white(anim * 30);
		gfx::Color title_color = gfx::Color::white(anim * 255);
		gfx::Color label_color = gfx::Color::white(anim * 120);
		gfx::Color value_color = gfx::Color::white(anim * 220);

		render::rounded_rect_filled(rect, background_color, POPOVER_ROUNDING);
		render::rounded_rect_stroke(rect, border_color, POPOVER_ROUNDING);

		gfx::Point pos = rect.origin() + gfx::Point(POPOVER_PADDING.w, POPOVER_PADDING.h);
		render::text(pos, title_color, title, font);

		pos.y += font.height() + TITLE_GAP;
		render::line(
			gfx::Point(rect.x + POPOVER_PADDING.w, pos.y),
			gfx::Point(rect.x2() - POPOVER_PADDING.w, pos.y),
			separator_color
		);
		pos.y += TITLE_GAP;

		for (const auto& row : rows) {
			render::text(pos, label_color, row.label, font);
			render::text(gfx::Point(pos.x + layout.value_x, pos.y), value_color, row.value, font);
			pos.y += layout.row_height;
		}
	});
}

void ui::render_info_icon(const Container& container, const AnimatedElement& element) {
	const auto& info_data = std::get<InfoIconElementData>(element.element->data);

	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;

	gfx::Color icon_color =
		gfx::Color::lerp(gfx::Color::white(150), gfx::Color::white(), hover_anim).adjust_alpha(anim);

	render::text(
		element.element->rect.center(), icon_color, icons::INFO, fonts::icons, FONT_CENTERED_X | FONT_CENTERED_Y
	);

	render_info_popover(element.element->rect, info_data.title, info_data.rows, info_data.font, hover_anim * anim);
}

bool ui::update_info_icon(const Container& container, AnimatedElement& element) {
	bool hovered = element.element->rect.contains(keys::mouse_pos) && set_hovered_element(element);
	element.animations.at(hasher("hover")).set_goal(hovered ? 1.f : 0.f);

	return false;
}

ui::AnimatedElement* ui::add_info_icon(
	const std::string& id,
	Container& container,
	const gfx::Size& size,
	const std::string& title,
	const std::vector<InfoRow>& rows,
	const render::Font& font
) {
	Element element(
		id,
		ElementType::INFO_ICON,
		gfx::Rect(container.current_position, size),
		InfoIconElementData{
			.title = title,
			.rows = rows,
			.font = font,
		},
		render_info_icon,
		update_info_icon
	);

	return add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(INFO_ICON_ANIMATION_SPEED) },
			{ hasher("hover"), AnimationState(INFO_ICON_HOVER_ANIMATION_SPEED) },
		}
	);
}
