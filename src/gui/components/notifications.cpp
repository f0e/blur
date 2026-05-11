#include "notifications.h"

#include "../ui/ui.h"
#include "../render/render.h"

namespace notifications = gui::components::notifications;

namespace {
	std::vector<notifications::Notification> notification_queue;
	std::mutex notification_mutex;
}

notifications::Notification& notifications::add(
	const std::string& id,
	const std::string& text,
	ui::NotificationType type,
	const std::optional<std::function<void(const std::string& id)>>& on_click,
	std::optional<std::chrono::duration<float>> duration,
	bool closable
) {
	std::lock_guard<std::mutex> lock(notification_mutex);

	Notification new_notification{
		.id = id,
		.end_time = duration ? std::optional(
								   std::chrono::steady_clock::now() +
								   std::chrono::duration_cast<std::chrono::steady_clock::duration>(*duration)
							   )
		                     : std::nullopt,
		.text = text,
		.type = type,
		.on_click_fn = on_click,
		.closable = closable,
	};

	for (auto& notification : notification_queue) {
		if (notification.id == id) {
			notification = new_notification;
			return notification;
		}
	}

	return notification_queue.emplace_back(new_notification);
}

notifications::Notification& notifications::add(
	const std::string& text,
	ui::NotificationType type,
	const std::optional<std::function<void(const std::string& id)>>& on_click,
	std::optional<std::chrono::duration<float>> duration,
	bool closable
) {
	static uint32_t current_notification_id = 0;
	return add(std::format("notification {}", current_notification_id++), text, type, on_click, duration, closable);
}

void notifications::close(const std::string& id) {
	std::lock_guard<std::mutex> lock(notification_mutex);

	for (auto& notification : notification_queue) {
		if (notification.id == id) {
			notification.closing = true;
			break;
		}
	}
}

void notifications::show_failure_notification(
	const std::string& error_header,
	const std::variant<std::string, rendering::RenderError>& error,
	std::optional<std::chrono::duration<float>> duration
) {
	std::string error_message;
	std::string clipboard_text;
	bool is_blur_error = true;

	if (std::holds_alternative<rendering::RenderError>(error)) {
		const auto& err = std::get<rendering::RenderError>(error);
		error_message = err.user_message;
		clipboard_text = err.to_string();
		is_blur_error = err.is_blur_exception;
	}
	else {
		const auto& err_str = std::get<std::string>(error);
		error_message = err_str;
		clipboard_text = err_str;
	}

	add(
		std::format("{}\n\n{}\n\nClick to copy detailed error log.", error_header, error_message),
		ui::NotificationType::NOTIF_ERROR,
		[clipboard_text](const std::string& id) {
			SDL_SetClipboardText(clipboard_text.c_str());

			close(id);
			add("Copied error message to clipboard", ui::NotificationType::INFO, {}, std::chrono::duration<float>(2.f));
		},
		duration
	);
}

void notifications::render(ui::Container& container) {
	std::lock_guard<std::mutex> lock(notification_mutex);

	auto now = std::chrono::steady_clock::now();

	for (auto it = notification_queue.begin(); it != notification_queue.end();) {
		if ((it->end_time && now > *it->end_time) || it->closing) {
			it = notification_queue.erase(it);
			continue;
		}

		ui::add_notification(
			it->id,
			container,
			it->text,
			it->type,
			fonts::dejavu,
			it->on_click_fn,
			it->closable ? std::optional<std::function<void(const std::string&)>>([](const std::string& id) {
				notifications::close(id);
			})
						 : std::nullopt
		);

		++it;
	}
}
