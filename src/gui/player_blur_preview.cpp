#include "player_blur_preview.h"
#include "ui/helpers/video.h"

PlayerBlurPreview::State PlayerBlurPreview::update(const Request& request) {
	if (!request.player.is_paused())
		return { .playing = true };

	// kept loaded while playing, so pausing again only has to seek
	if (!m_preview)
		m_preview = std::make_unique<BlurPreview>();

	// a seek's target, so the blurred frame's rendered alongside the player seeking there
	auto position = player_position(request.player, request.video_info);
	if (!position)
		return { .status = m_preview->status() };

	m_preview->update(
		{
			.video_path = request.video_path,
			.video_info = request.video_info,
			.settings = request.settings,
			.app_settings = request.app_settings,
			.position = *position,
		}
	);

	State state{ .status = m_preview->status() };

	if (auto ready = m_preview->ready_player()) {
		m_player_frames_at_blur = request.player.frame_count();
		state.overlay = ready;
		return state;
	}

	// the last blurred frame stays up until the player has a newer frame to stand in with
	if (request.player.frame_count() == m_player_frames_at_blur)
		state.overlay = m_preview->previous_player();

	return state;
}

std::optional<rendering::RenderError> PlayerBlurPreview::take_error() {
	return m_preview ? m_preview->take_error() : std::nullopt;
}

void PlayerBlurPreview::handle_event(const SDL_Event& event, bool& to_render) {
	if (m_preview)
		m_preview->handle_event(event, to_render);
}

std::optional<std::string> PlayerBlurPreview::status_text(const State& state) {
	if (state.playing)
		return "pause to see it blurred";

	if (state.overlay)
		return std::nullopt;

	if (state.status.failed)
		return "couldn't generate the preview";

	switch (state.status.init_stage) {
		case rendering::RenderState::InitStage::GENERATING_MASK:
			return "analysing video to generate a mask...";
		case rendering::RenderState::InitStage::BUILDING_ENGINE:
			return "building tensorrt engine, this may take a few minutes...";
		case rendering::RenderState::InitStage::NONE:
			break;
	}

	return std::nullopt;
}

std::optional<float> PlayerBlurPreview::player_position(const VideoPlayer& player, const media::VideoInfo& video_info) {
	auto time = player.get_time_pos();
	if (!time || video_info.video_duration <= 0.0)
		return std::nullopt;

	// mpv's clock starts with the container, which can be before the video's first frame
	double video_time = *time - (video_info.video_start_time - video_info.start_time);

	return static_cast<float>(std::clamp(video_time / video_info.video_duration, 0.0, 1.0));
}
