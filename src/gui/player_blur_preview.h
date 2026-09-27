#pragma once

#include "blur_preview.h"
#include "blur_sample.h"

class VideoPlayer;

// shows a config's output over a video player while it's paused. the blur can't keep up with playback, so the video
// plays as it is, but the output can be rendered from where it's paused and looped as a sample
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

		// the sample's, since it plays its own audio
		float volume = 0.f;
	};

	// after a seek the last blurred frame stays up until the video has a newer frame to stand in with
	PreviewState update(const Request& request);

	// renders the output from where the player is once it's paused, showing frames as they're made. it loops once
	// it's done, or once it's continued. moving or playing the player ends it
	void start_sample();

	// a rendering sample loops what it's rendered so far, a looping one ends, leaving the player where it started.
	// false if there's no sample
	bool continue_sample();

	void cancel_sample();

	std::optional<rendering::RenderError> take_error();

	void handle_event(const SDL_Event& event, bool& to_render);

	// how far through the video the player is, 0-1
	[[nodiscard]] static std::optional<float> player_position(
		const VideoPlayer& player, const media::VideoInfo& video_info
	);

private:
	struct Sample {
		std::filesystem::path video_path;
		BlurSettings settings;

		// the player's time when it started
		double player_time = 0.0;

		std::unique_ptr<BlurSample> sample;
	};

	BlurPreview m_preview;

	std::optional<uint64_t> m_player_frames_at_blur;

	bool m_sample_requested = false;
	std::optional<Sample> m_sample;
	std::optional<rendering::RenderError> m_sample_error;

	PreviewState update_preview(const Request& request);
	void begin_sample(const Request& request);
	void update_sample(PreviewState& state);
};
