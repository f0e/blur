#pragma once

#include "blur_preview.h"

class VideoPlayer;

// shows a config's output over a video player while it's paused. the blur can't keep up with playback, so the video
// plays as it is. main thread only
class PlayerBlurPreview {
public:
	struct Request {
		// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members) only lives for the call it's made for
		const VideoPlayer& player;
		const std::filesystem::path& video_path;
		const media::VideoInfo& video_info;
		const BlurSettings& settings;
		const GlobalAppSettings& app_settings;
		// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
	};

	// after a seek the last blurred frame stays up until the video has a newer frame to stand in with
	PreviewState update(const Request& request);

	// why the preview couldn't be loaded, once
	std::optional<rendering::RenderError> take_error() {
		return m_preview.take_error();
	}

	void handle_event(const SDL_Event& event, bool& to_render) {
		m_preview.handle_event(event, to_render);
	}

	// how far through the video the player is, 0-1
	[[nodiscard]] static std::optional<float> player_position(
		const VideoPlayer& player, const media::VideoInfo& video_info
	);

private:
	BlurPreview m_preview;

	// the player's frame count when a blurred frame was last up to date
	std::optional<uint64_t> m_player_frames_at_blur;
};
