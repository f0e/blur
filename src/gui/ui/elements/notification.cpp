#include <utility>

#include "../ui.h"
#include "../render.h"
#include "../utils.h"

#include "../../renderer.h"

const gfx::Size NOTIFICATION_TEXT_PADDING = { 10, 7 };

void ui::render_notification(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	static const float rounding = 7.8f;

	const auto& notification_data = std::get<NotificationElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	gfx::Color notification_color = 0;
	gfx::Color border_color = 0;
	gfx::Color text_color = 0;

	switch (notification_data.type) {
		case NotificationType::SUCCESS: {
			notification_color = gfx::rgba(0, 35, 0, 255);
			border_color = gfx::rgba(80, 100, 80, 255);
			text_color = gfx::rgba(100, 255, 100, 255);
			break;
		}
		case NotificationType::NOTIF_ERROR: {
			notification_color = gfx::rgba(35, 0, 0, 255);
			border_color = gfx::rgba(100, 80, 80, 255);
			text_color = gfx::rgba(255, 100, 100, 255);
			break;
		}
		case NotificationType::INFO:
		default: {
			notification_color = gfx::rgba(35, 35, 35, 255);
			border_color = gfx::rgba(100, 100, 100, 255);
			text_color = gfx::rgba(255, 255, 255, 255);
			break;
		}
	};

	notification_color = utils::adjust_color(notification_color, anim);
	border_color = utils::adjust_color(border_color, anim);
	text_color = utils::adjust_color(text_color, anim);

	// fill
	render::rounded_rect_filled(surface, element.element->rect, notification_color, rounding);

	// border
	render::rounded_rect_stroke(surface, element.element->rect, border_color, rounding);

	gfx::Point text_pos = element.element->rect.origin();
	text_pos.y += notification_data.font.getSize();
	text_pos.x += NOTIFICATION_TEXT_PADDING.w;
	text_pos.y += NOTIFICATION_TEXT_PADDING.h;

	for (const auto& line : notification_data.lines) {
		render::text(surface, text_pos, text_color, line, notification_data.font);
		text_pos.y += notification_data.line_height;
	}
}

ui::Element& ui::add_notification(
	const std::string& id, Container& container, const std::string& text, ui::NotificationType type, const SkFont& font
) {
	gfx::Size notification_size = { 230, 100 }; // height is a maximum to start with
	const int line_height = font.getSize() + 5;

	std::vector<std::string> lines =
		render::wrap_text(text, notification_size - NOTIFICATION_TEXT_PADDING * 2, font, line_height);

	notification_size.h = lines.size() * line_height;
	notification_size.h += NOTIFICATION_TEXT_PADDING.h * 2;

	// now that we've calculated the notification size, add text padding

	Element element(
		id,
		ElementType::NOTIFICATION,
		gfx::Rect(container.current_position, notification_size),
		NotificationElementData{
			.lines = lines,
			.type = type,
			.font = font,
			.line_height = line_height,
		},
		render_notification
	);

	return *add_element(
		container,
		std::move(element),
		container.line_height,
		{ {
			hasher("main"),
			{
				.speed = 5.f,
			},
		} }
	);
}
