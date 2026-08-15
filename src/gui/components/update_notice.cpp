#include "update_notice.h"

#include "common/config_app.h"

#include "notifications.h"
#include "../render/render.h"

namespace update_notice = gui::components::update_notice;

namespace {
	const std::string ELEMENT_ID = "update notice";

	const std::string ACTION_TEXT = updates::can_self_update() ? "install" : "download";
	const std::string DISMISS_TEXT = "dismiss";
	const std::string CANCEL_TEXT = "cancel";
	const std::string GITHUB_TEXT = "view on github";

	const std::string CHECK_NOTIFICATION_ID = "update check";

	struct {
		std::optional<updates::UpdateCheckRes> update;
		bool dismissed = false;
		bool updating = false;
		bool cancelling = false;
		bool checking = false;
		std::string status_text;
		float progress = 0.f;
	} state;

	std::mutex state_mutex;

	void dismiss() {
		std::string tag;

		{
			std::lock_guard<std::mutex> lock(state_mutex);
			if (!state.update || state.dismissed)
				return;

			state.dismissed = true;
			tag = state.update->latest_tag;
		}

		// remember the dismissal
		auto app_config = config_app::get_app_config();
		app_config.dismissed_update_version = tag;
		config_app::create(config_app::get_app_config_path(), app_config);
	}

	void set_progress(float progress, bool done) {
		std::lock_guard<std::mutex> lock(state_mutex);

		if (state.cancelling)
			return;

		state.progress = progress;
		state.status_text = done ? "opening installer" : std::format("downloading update  {:.0f}%", progress * 100.f);
	}

	void cancel_update() {
		std::lock_guard<std::mutex> lock(state_mutex);
		if (!state.updating || state.cancelling)
			return;

		// the download thread picks this up and bails, then the notice goes back to its normal state
		state.cancelling = true;
	}

	void stop_updating() {
		std::lock_guard<std::mutex> lock(state_mutex);
		state.updating = false;
		state.cancelling = false;
		state.progress = 0.f;
		state.status_text.clear();
	}

	void start_update(const std::string& tag, const std::string& tag_url) {
		if (!updates::can_self_update()) {
			SDL_OpenURL(tag_url.c_str());
			return;
		}

		{
			std::lock_guard<std::mutex> lock(state_mutex);
			if (state.updating)
				return;

			state.updating = true;
			state.cancelling = false;
			state.status_text = "downloading update";
			state.progress = 0.f;
		}

		std::thread([tag, tag_url] {
			bool success = Blur::update(
				tag,
				[](const std::string& text, float progress, bool done) {
					set_progress(progress, done);
				},
				[] {
					std::lock_guard<std::mutex> lock(state_mutex);
					return state.cancelling;
				}
			);

			bool cancelled = false;
			{
				std::lock_guard<std::mutex> lock(state_mutex);
				cancelled = state.cancelling;
			}

			stop_updating();

			if (cancelled)
				return;

			if (!success) {
				gui::components::notifications::add(
					"Failed to download the update. Click to open the download page.",
					ui::NotificationType::NOTIF_ERROR,
					[tag_url](const std::string& id) {
						SDL_OpenURL(tag_url.c_str());
					},
					std::chrono::duration<float>(10.f)
				);

				return;
			}

			// close so the installer can overwrite files
			blur.exiting = true;

			SDL_Event quit_event{};
			quit_event.type = SDL_EVENT_QUIT;
			SDL_PushEvent(&quit_event);
		}).detach();
	}
}

void update_notice::set_available(const updates::UpdateCheckRes& update) {
	if (update.is_latest)
		return;

	bool already_dismissed = config_app::get_app_config().dismissed_update_version == update.latest_tag;

	std::lock_guard<std::mutex> lock(state_mutex);
	state.update = update;
	state.dismissed = already_dismissed;
}

bool update_notice::is_updating() {
	std::lock_guard<std::mutex> lock(state_mutex);
	return state.updating;
}

void update_notice::check_now() {
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		if (state.checking || state.updating)
			return;

		state.checking = true;
	}

	std::thread([] {
		auto res = updates::is_latest_version(config_app::get_app_config().check_beta);

		{
			std::lock_guard<std::mutex> lock(state_mutex);
			state.checking = false;
		}

		if (!res) {
			gui::components::notifications::add(
				CHECK_NOTIFICATION_ID,
				std::format("Failed to check for updates: {}", res.error()),
				ui::NotificationType::NOTIF_ERROR
			);
			return;
		}

		if (res->is_latest) {
			gui::components::notifications::add(
				CHECK_NOTIFICATION_ID,
				std::format("You're on the latest version (v{})", BLUR_VERSION),
				ui::NotificationType::SUCCESS
			);
			return;
		}

		// they asked for the check, so show the notice even if this update was dismissed before
		std::lock_guard<std::mutex> lock(state_mutex);
		state.update = *res;
		state.dismissed = false;
	}).detach();
}

bool update_notice::is_checking() {
	std::lock_guard<std::mutex> lock(state_mutex);
	return state.checking;
}

void update_notice::render(ui::Container& container, ui::UpdateNoticeAlign align) {
	updates::UpdateCheckRes update;
	bool updating = false;
	std::string status_text;
	float progress = 0.f;

	{
		std::lock_guard<std::mutex> lock(state_mutex);
		if (!state.update || state.dismissed)
			return;

		update = *state.update;
		updating = state.updating;
		status_text = state.status_text;
		progress = state.progress;
	}

	if (updating) {
		// the rule turns into a progress bar while it downloads
		ui::add_update_notice(
			ELEMENT_ID,
			container,
			status_text,
			"",
			{ { { .text = CANCEL_TEXT, .on_press = cancel_update } } },
			progress,
			fonts::dejavu,
			align
		);
		return;
	}

	std::vector<ui::UpdateNoticeLink> top_line{
		{ .text = DISMISS_TEXT, .on_press = dismiss },
	};

	// on platforms without an installer the action link already goes to the release page
	if (updates::can_self_update()) {
		top_line.push_back(
			{
				.text = GITHUB_TEXT,
				.on_press =
					[url = update.latest_tag_url] {
						SDL_OpenURL(url.c_str());
					},
			}
		);
	}

	std::vector<ui::UpdateNoticeLink> bottom_line{
		{
			.text = ACTION_TEXT,
			.primary = true,
			.on_press =
				[tag = update.latest_tag, url = update.latest_tag_url] {
					start_update(tag, url);
				},
		},
	};

	ui::add_update_notice(
		ELEMENT_ID,
		container,
		"update available",
		std::format("v{} -> {}", BLUR_VERSION, update.latest_tag),
		{ top_line, bottom_line },
		{},
		fonts::dejavu,
		align
	);
}
