#pragma once

#include "common/config_app.h"
#include "common/config_blur.h"
#include "common/media.h"
#include "../../ui/frame.h"

class VideoPlayer;

namespace gui::components::configs::preview_frames {
	struct Request {
		// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members) only lives for the ui frame it's built in
		const std::filesystem::path& video_path;
		const BlurSettings& settings;
		const GlobalAppSettings& app_settings;
		// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
		float position = 0.f; // through the video, 0-1
		bool show_mask = false;
	};

	struct Result {
		std::optional<ui::Frame> frame;
		bool failed = false;
		bool playing = false;
		float video_duration = 0.f;
		std::string frame_timing_log;
		std::optional<std::string> status;
		std::optional<float> playback_position;

		// what the timeline follows and seeks
		std::shared_ptr<VideoPlayer> timeline_player;

		// what a pre-render covers, for the timeline to mark. as fractions of the container's duration
		std::optional<std::pair<float, float>> pre_render_range;

		std::optional<media::VideoInfo> video_info;
	};

	Result update(const Request& request);

	void handle_event(const SDL_Event& event, bool& to_render);

	// shift+space starts a pre-render, space moves it on
	void handle_key_press(SDL_Keycode key, SDL_Keymod mod);

	void toggle_playback();

	void pause();

	bool save_mask(const std::filesystem::path& path, std::function<void(std::optional<std::string> error)> on_done);

	void reset();
}
