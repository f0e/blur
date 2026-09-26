#pragma once

#include "common/config_app.h"
#include "common/config_blur.h"

class VideoPlayer;

namespace gui::components::configs::preview_frames {
	struct Request {
		// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members) only lives for the ui frame it's built in
		const std::filesystem::path& video_path;
		const BlurSettings& settings;
		const GlobalAppSettings& app_settings;
		// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
		bool show_mask = false;
	};

	struct Frame {
		std::shared_ptr<VideoPlayer> player;

		// the source video standing in until the blurred frame's ready
		bool faded = false;
	};

	struct Result {
		std::optional<Frame> frame;
		bool loading = false;
		bool failed = false;
		bool playing = false;
		float video_duration = 0.f;
		std::string frame_timing_log;

		// what to tell the user while there's nothing blurred to show
		std::optional<std::string> status;

		// where the player is, for the seek bar to follow playback and frame stepping
		std::optional<float> playback_position;
	};

	Result update(const Request& request);

	void handle_event(const SDL_Event& event, bool& to_render);

	void handle_key_press(SDL_Keycode key);

	void toggle_playback();

	void pause();

	// false if the mask preview isn't showing an up to date mask
	bool save_mask(const std::filesystem::path& path, std::function<void(std::optional<std::string> error)> on_done);

	void reset();
}
