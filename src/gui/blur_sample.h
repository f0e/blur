#pragma once

#include "common/config_app.h"
#include "common/config_blur.h"
#include "common/media.h"
#include "common/rendering/render_errors.h"
#include "common/rendering/render_state.h"
#include "common/rendering/render_types.h"

class VideoPlayer;

namespace render {
	class Texture;
}

// renders a stretch of blur.py's output in the background, showing frames as they're made, then loops it. main
// thread only
class BlurSample {
public:
	// how long settings have to stay the same before a sample's rendered for them, so dragging a slider doesn't start
	// a render every frame
	static constexpr auto SETTLE_TIME = std::chrono::milliseconds(500);

	struct Request {
		// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members) only lives for the call it's made for
		const std::filesystem::path& video_path;
		const media::VideoInfo& video_info;
		const BlurSettings& settings;
		const GlobalAppSettings& app_settings;
		// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)

		// source frames
		size_t start_frame = 0;
		size_t end_frame = 0;

		float volume = 0.f;
	};

	explicit BlurSample(const Request& request);
	~BlurSample();

	BlurSample(const BlurSample&) = delete;
	BlurSample(BlurSample&&) = delete;
	BlurSample& operator=(const BlurSample&) = delete;
	BlurSample& operator=(BlurSample&&) = delete;

	// call every ui frame
	void update();

	// stops rendering and loops what's been rendered. false if there isn't enough to
	bool finish();

	[[nodiscard]] bool finishing() const;

	// the render failed or was stopped, there's nothing more coming
	[[nodiscard]] bool failed() const {
		return m_failed;
	}

	// the looping player, once it's showing a frame
	[[nodiscard]] std::shared_ptr<VideoPlayer> player() const;

	// the latest frame it's rendered, while it's still rendering. the id changes with the frame
	[[nodiscard]] std::shared_ptr<render::Texture> frame() const {
		return m_frame;
	}

	[[nodiscard]] size_t frame_id() const {
		return m_frame_id;
	}

	// the source frame after the last one it looped, once it's looping. it can stop short of the end it was given
	[[nodiscard]] std::optional<size_t> looped_end_frame() const;

	[[nodiscard]] rendering::RenderState::Progress progress() const {
		return m_state->get_progress();
	}

	std::optional<rendering::RenderError> take_error() {
		return std::exchange(m_error, std::nullopt);
	}

	void handle_event(const SDL_Event& event, bool& to_render);

private:
	std::filesystem::path m_path;
	std::shared_ptr<rendering::RenderState> m_state = std::make_shared<rendering::RenderState>();
	std::future<tl::expected<rendering::RenderResult, rendering::RenderError>> m_render;

	float m_volume;
	bool m_hardware_decoding;

	size_t m_start_frame;
	size_t m_end_frame;
	double m_fps;

	double m_speed = 1.0;

	std::shared_ptr<render::Texture> m_frame;
	size_t m_frame_id = 0;

	std::shared_ptr<VideoPlayer> m_player;

	bool m_failed = false;
	std::optional<rendering::RenderError> m_error;
};
