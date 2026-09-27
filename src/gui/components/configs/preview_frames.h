#pragma once

#include "common/config_app.h"
#include "common/config_blur.h"
#include "common/media.h"

class VideoPlayer;

namespace render {
	class Texture;
}

namespace gui::components::configs::preview_frames {
	struct Request {
		// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members) only lives for the ui frame it's built in
		const std::filesystem::path& video_path;
		const BlurSettings& settings;
		const GlobalAppSettings& app_settings;
		// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
		float position = 0.f; // through the video, 0-1
		bool show_mask = false;

		// loops a render of this range instead, as fractions of the container's duration like the timeline's
		std::optional<std::pair<float, float>> loop;

		// the range is being dragged, so the preview follows the source rather than showing the loop
		bool editing_loop = false;
	};

	// a render of part of the video
	struct Render {
		float offset = 0.f; // where its start is in the video, in seconds on mpv's clock
		float speed = 1.f;  // how far through the video a second of it goes
	};

	struct Frame {
		std::shared_ptr<VideoPlayer> player;

		// shown instead of the player when it's set. the id changes with the texture
		std::shared_ptr<render::Texture> texture;
		size_t texture_id = 0;

		bool faded = false;
	};

	struct Result {
		std::optional<Frame> frame;
		bool failed = false;
		bool playing = false;
		float video_duration = 0.f;
		std::string frame_timing_log;
		std::optional<std::string> status;
		std::optional<float> playback_position;

		// what the timeline follows and seeks. while looping it's the loop's render
		std::shared_ptr<VideoPlayer> timeline_player;
		std::optional<Render> timeline_render;

		std::shared_ptr<VideoPlayer> source_player;

		// what a sample covers, for the timeline to mark. as fractions of the container's duration
		std::optional<std::pair<float, float>> sample_range;

		std::optional<media::VideoInfo> video_info;
	};

	Result update(const Request& request);

	void handle_event(const SDL_Event& event, bool& to_render);

	// shift+space starts a sample, space moves it on. while a range is looping the keys go to the loop
	void handle_key_press(SDL_Keycode key, SDL_Keymod mod);

	void toggle_playback();

	void pause();

	bool save_mask(const std::filesystem::path& path, std::function<void(std::optional<std::string> error)> on_done);

	void reset();
}
