#pragma once

#include "../ui/ui.h"
#include "common/rendering.h"

namespace gui::components::notifications {
	inline constexpr float NOTIFICATION_LENGTH = 4.f;

	inline constexpr int NOTIFICATIONS_PAD_X = 10;
	inline constexpr int NOTIFICATIONS_PAD_Y = 10;

	struct Notification {
		std::string id;
		std::optional<std::chrono::steady_clock::time_point> end_time;
		std::string text;
		ui::NotificationType type;
		std::optional<std::function<void(const std::string& id)>> on_click_fn;
		bool closable = true;
		bool closing = false;
	};

	Notification& add(
		const std::string& id,
		const std::string& text,
		ui::NotificationType type,
		const std::optional<std::function<void(const std::string& id)>>& on_click = {},
		std::optional<std::chrono::duration<float>> duration = std::chrono::duration<float>(NOTIFICATION_LENGTH),
		bool closable = true
	);

	Notification& add(
		const std::string& text,
		ui::NotificationType type,
		const std::optional<std::function<void(const std::string& id)>>& on_click = {},
		std::optional<std::chrono::duration<float>> duration = std::chrono::duration<float>(NOTIFICATION_LENGTH),
		bool closable = true
	);

	void show_failure_notification(
		const std::string& error_header,
		const std::variant<std::string, rendering::RenderError>& error,
		std::optional<std::chrono::duration<float>> duration
	);

	void close(const std::string& id);

	void render(ui::Container& container);
}
