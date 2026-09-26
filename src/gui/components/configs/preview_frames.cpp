#include "preview_frames.h"

#include "../notifications.h"
#include "../../mask_preview.h"
#include "../../player_blur_preview.h"
#include "../../ui/helpers/video.h"
#include "common/media.h"

namespace preview_frames = gui::components::configs::preview_frames;

namespace {
	std::unique_ptr<PlayerBlurPreview> blurred_preview;
	std::unique_ptr<MaskPreview> mask_preview;
	bool showing_mask = false;

	std::shared_ptr<VideoPlayer> source_player;
	std::optional<float> source_timestamp;

	// the seek bar follows playback, which isn't a seek
	std::optional<float> playback_position;

	struct {
		std::mutex mutex;
		std::filesystem::path path;
		media::VideoInfo info;
	} video;

	void fetch_video_info(const std::filesystem::path& path) {
		std::thread([path] {
			auto info = media::get_video_info(path);

			std::lock_guard lock(video.mutex);

			if (video.path == path)
				video.info = info;
		}).detach();
	}

	// put on the frame the blurred frame for this position is centred on, so nothing jumps when that arrives
	void update_source_player(const preview_frames::Request& request, const media::VideoInfo& info, float position) {
		if (source_player && position == playback_position) {
			// wherever it was last put, it's somewhere else now
			source_timestamp.reset();
			return;
		}

		if (info.fps_num <= 0 || info.fps_den <= 0)
			return;

		double fps = static_cast<double>(info.fps_num) / info.fps_den;
		double frame_time = std::round(BlurPreview::source_time(request.settings, info, position) * fps) / fps;

		// mpv's clock starts with the container, which can be before the video's first frame
		auto timestamp = VideoPlayer::frame_seek_target(info.video_start_time - info.start_time + frame_time, fps);

		if (!source_player) {
			source_player = std::make_shared<VideoPlayer>(0.f, request.app_settings.preview_hardware_decoding);
			source_player->load_file(request.video_path, timestamp);
			source_timestamp = timestamp;
			return;
		}

		source_player->set_hardware_decoding(request.app_settings.preview_hardware_decoding);

		if (timestamp != source_timestamp) {
			source_timestamp = timestamp;
			source_player->seek(timestamp, true);
		}
	}

	void show_error(const std::string& header, const std::optional<rendering::RenderError>& error) {
		if (error)
			gui::components::notifications::show_failure_notification(
				header, *error, std::chrono::duration<float>(10.f)
			);
	}

	preview_frames::Result update_mask(const preview_frames::Request& request, const media::VideoInfo& info) {
		if (source_player && !source_player->is_paused())
			source_player->set_paused(true);

		if (!mask_preview)
			mask_preview = std::make_unique<MaskPreview>();

		auto state = mask_preview->update(
			{
				.video_path = request.video_path,
				.video_info = info,
				.settings = request.settings,
				.app_settings = request.app_settings,
			}
		);

		show_error("Failed to generate mask preview.", mask_preview->take_error());

		preview_frames::Result result{
			.failed = state.status.failed,
			.video_duration = static_cast<float>(info.video_duration),
			.status = state.status_text(),
		};

		if (state.overlay)
			result.frame = preview_frames::Frame{ .player = state.overlay };

		return result;
	}

	preview_frames::Result update_blurred(const preview_frames::Request& request, const media::VideoInfo& info) {
		update_source_player(request, info, request.app_settings.config_preview_seek);

		preview_frames::Result result{ .video_duration = static_cast<float>(info.video_duration) };

		if (!source_player)
			return result;

		if (!blurred_preview)
			blurred_preview = std::make_unique<PlayerBlurPreview>();

		auto state = blurred_preview->update(
			{
				.player = *source_player,
				.video_path = request.video_path,
				.video_info = info,
				.settings = request.settings,
				.app_settings = request.app_settings,
			}
		);

		show_error("Failed to generate config preview.", blurred_preview->take_error());

		result.playing = state.playing;
		result.failed = state.status.failed;
		result.frame_timing_log = state.status.frame_timing_log;
		result.status = state.status_text();

		// the seek bar follows playback and frame stepping. a seek that's on its way would put it back where it came
		// from
		if (source_player->seek_settled()) {
			playback_position = PlayerBlurPreview::player_position(*source_player, info);
			result.playback_position = playback_position;
		}

		if (state.overlay)
			result.frame = preview_frames::Frame{ .player = state.overlay };
		else if (source_player->has_frame() && source_player->get_video_dimensions())
			result.frame = preview_frames::Frame{ .player = source_player, .faded = !state.playing };

		return result;
	}
}

preview_frames::Result preview_frames::update(const Request& request) {
	bool video_changed = false;
	{
		std::lock_guard lock(video.mutex);
		video_changed = video.path != request.video_path;
	}

	if (video_changed) {
		reset();

		{
			std::lock_guard lock(video.mutex);
			video.path = request.video_path;
		}

		if (!request.video_path.empty())
			fetch_video_info(request.video_path);
	}

	if (request.video_path.empty())
		return {};

	media::VideoInfo info;
	{
		std::lock_guard lock(video.mutex);
		info = video.info;
	}

	if (!info.has_video_stream)
		return { .video_duration = static_cast<float>(info.video_duration) };

	showing_mask = request.show_mask;

	if (request.show_mask)
		return update_mask(request, info);

	return update_blurred(request, info);
}

void preview_frames::handle_key_press(SDL_Keycode key) {
	if (source_player && !showing_mask)
		source_player->handle_key_press(key);
}

void preview_frames::toggle_playback() {
	if (source_player && !showing_mask)
		source_player->cycle_paused();
}

void preview_frames::pause() {
	if (source_player && !source_player->is_paused())
		source_player->set_paused(true);
}

void preview_frames::handle_event(const SDL_Event& event, bool& to_render) {
	if (source_player)
		source_player->handle_mpv_event(event, to_render, true);

	if (blurred_preview)
		blurred_preview->handle_event(event, to_render);

	if (mask_preview)
		mask_preview->handle_event(event, to_render);
}

bool preview_frames::save_mask(
	const std::filesystem::path& path, std::function<void(std::optional<std::string> error)> on_done
) {
	return mask_preview && mask_preview->save(path, std::move(on_done));
}

void preview_frames::reset() {
	blurred_preview.reset();
	mask_preview.reset();

	source_player.reset();
	source_timestamp.reset();
	playback_position.reset();

	std::lock_guard lock(video.mutex);
	video.path.clear();
	video.info = {};
}
