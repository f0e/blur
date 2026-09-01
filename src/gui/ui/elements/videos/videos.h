#pragma once

#include "../../ui.h"

namespace ui::videos {
	inline constexpr int VIDEO_GAP = 30;
	inline constexpr int TIMELINE_GAP = 10;
	inline constexpr int TIMELINE_HEIGHT = 40;

	inline constexpr float START_FADE = 0.5f;

	inline std::shared_ptr<VideoPlayer> player;

	[[nodiscard]] bool is_loaded(const std::filesystem::path& path);

	[[nodiscard]] float aspect_ratio(const UIVideo& video);

	[[nodiscard]] gfx::Size video_size(const Container& container, float aspect_ratio);

	[[nodiscard]] gfx::Rect timeline_rect(const gfx::Rect& video_rect);

	[[nodiscard]] VideoWaveform* get_waveform(const std::filesystem::path& path, float duration);

	void init_zoom(AnimatedElement& timeline, float duration);

	void update_progress(AnimatedElement& timeline);
}
