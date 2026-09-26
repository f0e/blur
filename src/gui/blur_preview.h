#pragma once

#include "common/config_app.h"
#include "common/config_blur.h"
#include "common/media.h"
#include "common/rendering/render_errors.h"
#include "common/rendering/render_state.h"

class VideoPlayer;

// plays blur.py's output for a video in mpv, running vapoursynth in this process, so it can be seeked around
// without rendering each frame from scratch. main thread only
class BlurPreview {
public:
	struct Request {
		// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members) only lives for the call it's made for
		const std::filesystem::path& video_path;
		const media::VideoInfo& video_info;
		const BlurSettings& settings;
		const GlobalAppSettings& app_settings;
		// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)

		float position = 0.f; // through the video, 0-1

		// the mask blur.py would apply, rather than the blurred video
		bool mask = false;
	};

	struct Status {
		rendering::RenderState::InitStage init_stage = rendering::RenderState::InitStage::NONE;
		std::string frame_timing_log;

		// the latest settings couldn't be loaded. cleared when different ones are requested
		bool failed = false;
	};

	BlurPreview() = default;
	~BlurPreview();

	BlurPreview(const BlurPreview&) = delete;
	BlurPreview(BlurPreview&&) = delete;
	BlurPreview& operator=(const BlurPreview&) = delete;
	BlurPreview& operator=(BlurPreview&&) = delete;

	// call every ui frame it's shown. changing anything but the position reloads blur.py
	void update(const Request& request);

	// the player to draw, once it's showing the frame for the latest request
	[[nodiscard]] std::shared_ptr<VideoPlayer> ready_player() const;

	// the frame it showed last, while it's on its way to a new position. nothing once it's reloading
	[[nodiscard]] std::shared_ptr<VideoPlayer> previous_player() const;

	[[nodiscard]] Status status() const;

	// why the latest settings couldn't be loaded, once
	std::optional<rendering::RenderError> take_error();

	void handle_event(const SDL_Event& event, bool& to_render);

	// how far into the source (from its first frame) the output frame shown for a position is centred, so the source
	// can be shown at the same point
	[[nodiscard]] static double source_time(
		const BlurSettings& settings, const media::VideoInfo& video_info, float position
	);

	// saves the frame for the latest request at the video's size. false if it isn't ready
	bool save_frame(
		const std::filesystem::path& path, std::function<void(std::optional<std::string> error)> on_done
	) const;

private:
	// what changes blur.py's output. the app settings are narrowed to what it's given, most (like the config
	// preview's seek) don't affect it
	struct Key {
		std::filesystem::path video_path;
		BlurSettings settings;
		std::string gpu_type;
		std::string rife_device;
		std::string tensorrt_device;
		bool mask = false;

		bool operator==(const Key& other) const = default;
	};

	struct PendingScript {
		Key key;
		media::VideoInfo video_info;
		std::future<tl::expected<std::string, std::string>> script;
	};

	std::shared_ptr<VideoPlayer> m_player;

	std::filesystem::path m_script_path;
	std::filesystem::path m_log_path;
	std::streamoff m_log_offset = 0;

	std::optional<Key> m_requested;
	float m_requested_position = 0.f;

	std::optional<PendingScript> m_pending;
	std::chrono::steady_clock::time_point m_last_build;

	std::optional<Key> m_loaded;
	std::optional<float> m_position;

	std::unique_ptr<rendering::RenderState> m_state = std::make_unique<rendering::RenderState>();
	std::optional<rendering::RenderError> m_error;
	bool m_failed = false;

	void start_build(const Request& request);
	void finish_build();
	void read_log();
	void fail(rendering::RenderError error);
};

// what a preview has to show at the moment
struct PreviewState {
	bool playing = false;

	// the preview's frame to show. while this is empty the video stands in faded
	std::shared_ptr<VideoPlayer> overlay;

	BlurPreview::Status status;

	// what to tell the user while there's nothing to show. loading_text is for when it's just on its way
	[[nodiscard]] std::optional<std::string> status_text(std::optional<std::string> loading_text = {}) const;
};
