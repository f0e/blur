#include "queue_preview.h"
#include "notifications.h"
#include "../mask_preview.h"
#include "../player_blur_preview.h"
#include "../ui/elements/videos/videos.h"
#include "../ui/helpers/video.h"

namespace queue_preview = gui::components::queue_preview;

namespace {
	std::unique_ptr<PlayerBlurPreview> blur_preview;
	std::unique_ptr<MaskPreview> mask_preview;

	// configs are read from disk, so the last one's kept until its file changes
	struct {
		std::string name;
		std::filesystem::file_time_type write_time;
		BlurSettings settings;
	} config;

	void show_error(const std::string& header, const std::optional<rendering::RenderError>& error) {
		if (error)
			gui::components::notifications::show_failure_notification(
				header, *error, std::chrono::duration<float>(10.f)
			);
	}
}

BlurSettings queue_preview::settings(const tasks::PendingVideo& pending_video) {
	std::error_code ec;
	auto write_time = std::filesystem::last_write_time(config_blur::get_config_path(pending_video.config_name), ec);

	if (pending_video.config_name != config.name || write_time != config.write_time) {
		config.name = pending_video.config_name;
		config.write_time = write_time;
		config.settings = config_blur::get_config(pending_video.config_name);
	}

	auto settings = config.settings;
	settings.mask = pending_video.mask;
	settings.auto_mask = pending_video.auto_mask;
	return settings;
}

queue_preview::State queue_preview::update(
	const tasks::PendingVideo& pending_video, const GlobalAppSettings& app_settings
) {
	if (!blur_enabled)
		blur_preview.reset();

	if (!mask_enabled)
		mask_preview.reset();

	if (!blur_enabled && !mask_enabled)
		return {};

	if (!pending_video.video_info || !pending_video.video_info->ffmpeg_can_decode_video)
		return {};

	if (pending_video.config_name.empty())
		return { .status = "select a config to preview it" };

	const auto& player = ui::videos::player;
	if (!player || !ui::videos::is_loaded(pending_video.video_path))
		return {};

	auto video_settings = settings(pending_video);

	if (mask_enabled) {
		if (!mask_preview)
			mask_preview = std::make_unique<MaskPreview>();

		// the mask's a still image
		if (!player->is_paused())
			player->set_paused(true);

		auto state = mask_preview->update(
			{
				.video_path = pending_video.video_path,
				.video_info = *pending_video.video_info,
				.settings = video_settings,
				.app_settings = app_settings,
			}
		);

		show_error("Failed to generate mask preview.", mask_preview->take_error());

		return {
			.overlay = ui::VideoOverlay{ .frame = state.frame },
			.status = state.status_text("loading mask..."),
		};
	}

	if (!blur_preview)
		blur_preview = std::make_unique<PlayerBlurPreview>();

	auto state = blur_preview->update(
		{
			.player = *player,
			.video_path = pending_video.video_path,
			.video_info = *pending_video.video_info,
			.settings = video_settings,
			.app_settings = app_settings,
			.volume = static_cast<float>(app_settings.preview_volume),
		}
	);

	show_error("Failed to generate blur preview.", blur_preview->take_error());

	State result{ .status = state.status_text("rendering preview...") };
	if (!state.playing)
		result.overlay = ui::VideoOverlay{
			.frame = state.frame,
			.range = state.pre_render_range,
		};

	return result;
}

void queue_preview::handle_event(const SDL_Event& event, bool& to_render) {
	if (blur_preview)
		blur_preview->handle_event(event, to_render);

	if (mask_preview)
		mask_preview->handle_event(event, to_render);
}

bool queue_preview::handle_key_press(SDL_Keycode key, SDL_Keymod mod) {
	const auto& player = ui::videos::player;
	if (key != SDLK_SPACE || !player)
		return false;

	if (blur_preview && blur_preview->continue_pre_render())
		return true;

	if (!(mod & SDL_KMOD_SHIFT))
		return false;

	// a pre-render's shown over the blurred preview, so it's turned on for it
	blur_enabled = true;
	mask_enabled = false;

	if (!blur_preview)
		blur_preview = std::make_unique<PlayerBlurPreview>();

	player->set_paused(true);
	blur_preview->start_pre_render();
	return true;
}

void queue_preview::release() {
	blur_preview.reset();
	mask_preview.reset();
}
