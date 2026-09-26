#include "preview_frames.h"

#include "../notifications.h"
#include "../../blur_preview.h"
#include "../../ui/helpers/video.h"
#include "common/media.h"

namespace preview_frames = gui::components::configs::preview_frames;

namespace {
	std::unique_ptr<BlurPreview> blurred_preview;
	std::unique_ptr<BlurPreview> mask_preview;

	// a mask is worked out from the whole video, so it only needs reloading when the masking settings change
	std::optional<BlurSettings> mask_settings;

	std::shared_ptr<VideoPlayer> source_player;
	std::optional<float> source_timestamp;

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

	// kept on the frame the blurred frame for this position is centred on, so nothing jumps when that arrives
	void update_source_player(const preview_frames::Request& request, const media::VideoInfo& info, float position) {
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

	BlurPreview& get_preview(bool mask) {
		auto& preview = mask ? mask_preview : blurred_preview;
		if (!preview)
			preview = std::make_unique<BlurPreview>();

		return *preview;
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

	Result result{
		.loading = true,
		.video_duration = static_cast<float>(info.video_duration),
	};

	if (!info.has_video_stream)
		return result;

	float position = request.app_settings.config_preview_seek;

	const BlurSettings* settings = &request.settings;
	if (request.show_mask) {
		if (!mask_settings || !config_blur::same_masking(*mask_settings, request.settings))
			mask_settings = request.settings;

		settings = &*mask_settings;
	}

	auto& preview = get_preview(request.show_mask);

	preview.update(
		{
			.video_path = request.video_path,
			.video_info = info,
			.settings = *settings,
			.app_settings = request.app_settings,
			.position = position,
			.mask = request.show_mask,
		}
	);

	if (auto error = preview.take_error()) {
		gui::components::notifications::show_failure_notification(
			request.show_mask ? "Failed to generate mask preview." : "Failed to generate config preview.",
			*error,
			std::chrono::duration<float>(10.f)
		);
	}

	auto status = preview.status();
	result.init_stage = status.init_stage;
	result.frame_timing_log = status.frame_timing_log;
	result.failed = status.failed;

	if (auto player = preview.ready_player()) {
		result.frame = Frame{ .player = player, .up_to_date = true };
		result.loading = false;
		return result;
	}

	result.loading = !status.failed;

	// the source stands in for the blurred video, but isn't anything like a mask
	if (!request.show_mask) {
		update_source_player(request, info, position);

		if (source_player && source_player->has_frame() && source_player->get_video_dimensions())
			result.frame = Frame{ .player = source_player };
	}

	return result;
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
	return mask_preview && mask_preview->save_frame(path, std::move(on_done));
}

void preview_frames::reset() {
	blurred_preview.reset();
	mask_preview.reset();
	mask_settings.reset();

	source_player.reset();
	source_timestamp.reset();

	std::lock_guard lock(video.mutex);
	video.path.clear();
	video.info = {};
}
