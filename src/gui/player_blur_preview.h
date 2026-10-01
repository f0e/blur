#pragma once

#include "blur_preview.h"
#include "pre_render.h"

class VideoPlayer;

// shows a config's output over a video player while it's paused. the blur can't keep up with playback, so the video
// plays as it is, but the output can be pre-rendered from where it's paused and looped
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

		// the pre-render's, since it plays its own audio
		float volume = 0.f;
	};

	// after a seek the last blurred frame stays up until the video has a newer frame to stand in with
	PreviewState update(const Request& request);

	// renders the output from where the player is once it's paused, showing frames as they're made. it loops once
	// it's done, or once it's continued. moving or playing the player ends it, changing the settings renders it again
	void start_pre_render();

	// a pre-render that's rendering loops what it's rendered so far, a looping one ends, leaving the player where it
	// started. false if there's no pre-render
	bool continue_pre_render();

	void cancel_pre_render();

	std::optional<rendering::RenderError> take_error();

	void handle_event(const SDL_Event& event, bool& to_render);

	// how far through the video the player is, 0-1
	[[nodiscard]] static std::optional<float> player_position(
		const VideoPlayer& player, const media::VideoInfo& video_info
	);

private:
	// so dragging a slider doesn't start a render every frame
	static constexpr auto SETTLE_TIME = std::chrono::milliseconds(500);

	// what a pre-render's output depends on, so it's only rendered again when that changes
	struct PreRenderSettings {
		BlurSettings blur;
		bool output_encoding = false;
		bool fully_blur_first_frame = false;

		bool operator==(const PreRenderSettings& other) const = default;
	};

	struct ActivePreRender {
		std::unique_ptr<PreRender> render;
		std::filesystem::path video_path;
		PreRenderSettings settings;

		// the player's time when it started
		double player_time = 0.0;

		// settings it's waiting on to settle before it's rendered again
		std::optional<PreRenderSettings> new_settings;
		std::chrono::steady_clock::time_point new_settings_since;
	};

	BlurPreview m_preview;

	std::optional<uint64_t> m_player_frames_at_blur;

	bool m_pre_render_requested = false;
	std::optional<ActivePreRender> m_pre_render;
	std::optional<rendering::RenderError> m_pre_render_error;

	static PreRenderSettings pre_render_settings(const Request& request);
	static std::unique_ptr<PreRender> make_pre_render(const Request& request, size_t start_frame, size_t end_frame);

	PreviewState update_preview(const Request& request);
	void begin_pre_render(const Request& request);
	void rerender(const Request& request, const PreRenderSettings& settings);
	void update_pre_render(const Request& request, PreviewState& state);
};
