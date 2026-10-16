#pragma once

#include "../../ui.h"
#include "../../helpers/video.h"

namespace ui::videos {
	inline constexpr int VIDEO_GAP = 30;
	inline constexpr int TIMELINE_GAP = 10;
	inline constexpr int TIMELINE_HEIGHT = 40;
	// per audio track, when there's room
	inline constexpr int TRACK_HEIGHT = TIMELINE_HEIGHT;
	// the track numbers to the left of a timeline with more than one
	inline constexpr int TRACK_LABEL_WIDTH = 12;
	inline constexpr int TRACK_LABEL_GAP = 4;
	// to the right, when the tracks don't fit and scroll
	inline constexpr int TRACK_SCROLLBAR_WIDTH = 14;

	inline constexpr float START_FADE = 0.5f;

	inline std::shared_ptr<VideoPlayer> player;

	[[nodiscard]] bool is_loaded(const std::filesystem::path& path);

	[[nodiscard]] float aspect_ratio(const UIVideo& video);

	[[nodiscard]] gfx::Size video_size(const Container& container, float aspect_ratio);

	[[nodiscard]] size_t audio_track_count(const UIVideo& video);

	struct Layout {
		int y;
		int timeline_height;
	};

	// the video and its timeline are centred between top and bottom. when the timeline wouldn't fit its tracks, they
	// move up to the top and the timeline takes what's left
	[[nodiscard]] Layout layout(int top, int bottom, int video_h, const UIVideo& video);

	[[nodiscard]] gfx::Rect timeline_rect(const gfx::Rect& video_rect, int height);

	[[nodiscard]] VideoWaveform* get_waveform(const std::filesystem::path& path, size_t track_count, float duration);

	// progress is where the playhead starts, as a fraction of the duration
	[[nodiscard]] std::unordered_map<size_t, AnimationState> timeline_animations(float duration, float progress = 0.f);

	void init_zoom(AnimatedElement& timeline, float duration);

	void update_progress(AnimatedElement& timeline);
}
