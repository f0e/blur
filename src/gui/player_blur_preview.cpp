#include "player_blur_preview.h"
#include "ui/helpers/video.h"

PreviewState PlayerBlurPreview::update(const Request& request) {
	// the blurred preview's kept loaded while playing, so pausing again only has to seek
	if (!request.player.is_paused())
		return { .playing = true };

	// mpv gives a seek's target as the position straight away, so the blurred frame renders alongside the seek
	auto position = player_position(request.player, request.video_info);
	if (!position)
		return { .status = m_preview.status() };

	m_preview.update(
		{
			.video_path = request.video_path,
			.video_info = request.video_info,
			.settings = request.settings,
			.app_settings = request.app_settings,
			.position = *position,
		}
	);

	PreviewState state{ .status = m_preview.status() };

	if (auto ready = m_preview.ready_player()) {
		m_player_frames_at_blur = request.player.frame_count();
		state.overlay = ready;
	}
	else if (request.player.frame_count() == m_player_frames_at_blur) {
		state.overlay = m_preview.previous_player();
	}

	return state;
}

std::optional<float> PlayerBlurPreview::player_position(const VideoPlayer& player, const media::VideoInfo& video_info) {
	auto time = player.get_time_pos();
	if (!time || video_info.video_duration <= 0.0)
		return std::nullopt;

	// mpv's clock starts with the container, which can be before the video's first frame
	double video_time = *time - (video_info.video_start_time - video_info.start_time);

	return static_cast<float>(std::clamp(video_time / video_info.video_duration, 0.0, 1.0));
}
