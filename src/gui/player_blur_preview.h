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

	struct State {
		bool playing = false;

		// while paused, the blurred frame once it's ready. until then the video stands in faded
		std::shared_ptr<VideoPlayer> overlay;

		BlurPreview::Status status;
	};

	State update(const Request& request);

	// why the preview couldn't be loaded, once
	std::optional<rendering::RenderError> take_error();

	void handle_event(const SDL_Event& event, bool& to_render);

	// what to tell the user while there's nothing blurred to show. nothing when it's just on its way
	[[nodiscard]] static std::optional<std::string> status_text(const State& state);

	// how far through the video the player is, 0-1
	[[nodiscard]] static std::optional<float> player_position(
		const VideoPlayer& player, const media::VideoInfo& video_info
	);

private:
	std::unique_ptr<BlurPreview> m_preview;
};
