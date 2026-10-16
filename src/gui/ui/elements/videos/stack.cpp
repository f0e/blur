#include "videos.h"

constexpr float OFFSET_ANIMATION_SPEED = 25.f;
constexpr float OFFSET_ANIMATION_SNAP = 0.0005f;
constexpr float ASPECT_ANIMATION_SPEED = 15.f;

namespace {
	std::optional<std::filesystem::path> loaded_path;

	float animation_current(
		const ui::Container& container, const std::string& element_id, size_t animation, float fallback
	) {
		auto element = container.elements.find(element_id);
		if (element == container.elements.end())
			return fallback;

		auto it = element->second.animations.find(animation);
		if (it == element->second.animations.end())
			return fallback;

		return it->second.current;
	}

	float stack_position(const std::vector<float>& stack_offsets, float position) {
		if (stack_offsets.empty())
			return 0.f;

		float clamped = std::clamp(position, 0.f, static_cast<float>(stack_offsets.size() - 1));
		auto low = static_cast<size_t>(clamped);
		size_t high = std::min(low + 1, stack_offsets.size() - 1);

		return std::lerp(stack_offsets[low], stack_offsets[high], clamped - static_cast<float>(low));
	}
}

bool ui::videos::is_loaded(const std::filesystem::path& path) {
	if (!player)
		return false;

	auto current = player->get_current_file_path();
	return current && *current == path;
}

float ui::videos::aspect_ratio(const UIVideo& video) {
	if (video.video_info)
		return static_cast<float>(video.video_info->width) / static_cast<float>(video.video_info->height);

	return 16.f / 9.f;
}

gfx::Size ui::videos::video_size(const Container& container, float aspect_ratio) {
	gfx::Size max_size(container.get_usable_rect().w, container.get_usable_rect().h / 1.5f);

	gfx::Size size = max_size;

	float target_width = size.h * aspect_ratio;
	float target_height = size.w / aspect_ratio;

	if (target_width <= max_size.w) {
		size.w = static_cast<int>(target_width);
	}
	else {
		size.h = static_cast<int>(target_height);
	}

	if (size.h > max_size.h) {
		size.h = max_size.h;
		size.w = static_cast<int>(max_size.h * aspect_ratio);
	}

	if (size.w > max_size.w) {
		size.w = max_size.w;
		size.h = static_cast<int>(max_size.w / aspect_ratio);
	}

	return size;
}

size_t ui::videos::audio_track_count(const UIVideo& video) {
	return video.video_info ? video.video_info->audio_sample_rates.size() : 0;
}

ui::videos::Layout ui::videos::layout(int top, int bottom, int video_h, const UIVideo& video) {
	int wanted = std::max(TIMELINE_HEIGHT, static_cast<int>(audio_track_count(video)) * TRACK_HEIGHT);

	int y = std::max(top, (top + bottom - (video_h + TIMELINE_GAP + wanted)) / 2);
	int space = bottom - y - video_h - TIMELINE_GAP;

	return { .y = y, .timeline_height = std::max(TIMELINE_HEIGHT, std::min(wanted, space)) };
}

gfx::Rect ui::videos::timeline_rect(const gfx::Rect& video_rect, int height) {
	return gfx::Rect{
		gfx::Point(video_rect.x, video_rect.y2()).offset_y(TIMELINE_GAP),
		gfx::Size(video_rect.w, height),
	};
}

void ui::handle_videos_event(const SDL_Event& event, bool& to_render) {
	if (!videos::player)
		return;

	switch (event.type) {
		case SDL_EVENT_KEY_DOWN:
			videos::player->handle_key_press(event.key.key);
			break;

		default:
			videos::player->handle_mpv_event(event, to_render, true);
			break;
	}
}

void ui::add_videos(
	const std::string& id,
	Container& container,
	int top,
	const std::vector<UIVideo>& ui_videos,
	size_t& index,
	float& start,
	float& end,
	float& volume,
	bool hardware_decoding,
	bool trim_disabled,
	const std::function<void(size_t video_id)>& on_remove,
	const std::optional<VideoOverlay>& active_overlay
) {
	if (ui_videos.empty() || index >= ui_videos.size())
		return;

	if (!videos::player)
		videos::player = std::make_shared<VideoPlayer>(volume, hardware_decoding);
	else
		videos::player->set_hardware_decoding(hardware_decoding);

	if (trim_disabled && (start != 0.f || end != 1.f)) {
		start = 0.f;
		end = 1.f;
		videos::player->set_start(start);
		videos::player->set_end(end);
	}

	const auto& active_video = ui_videos[index];

	bool switched = loaded_path && *loaded_path != active_video.path;
	loaded_path = active_video.path;

	// mpv decodes with ffmpeg too, so it can't play these (e.g. frameserver signposts)
	bool playable = !active_video.video_info || active_video.video_info->ffmpeg_can_decode_video;

	// before loading, so it doesn't start on the previous video's track
	videos::player->set_audio_track(active_video.audio_track ? *active_video.audio_track : 0);

	if (!playable) {
		if (videos::player->get_current_file_path())
			videos::player->stop();
	}
	else if (!videos::is_loaded(active_video.path)) {
		videos::player->load_file(active_video.path);
		videos::player->set_paused(true);
	}

	if (active_video.start && active_video.end) {
		videos::player->set_start(*active_video.start);
		videos::player->set_end(*active_video.end);
	}

	auto usable_rect = container.get_usable_rect();
	top = std::max(top, usable_rect.y);

	auto video_layout = [&](const UIVideo& ui_video, int video_h) {
		return videos::layout(top, usable_rect.y2(), video_h, ui_video);
	};

	std::vector<float> stack_offsets;
	stack_offsets.reserve(ui_videos.size());

	float stack_offset = 0.f;
	for (const auto& ui_video : ui_videos) {
		stack_offsets.push_back(stack_offset);

		int video_h = videos::video_size(container, videos::aspect_ratio(ui_video)).h;
		stack_offset +=
			video_h + videos::TIMELINE_GAP + video_layout(ui_video, video_h).timeline_height + videos::VIDEO_GAP;
	}

	std::string active_video_id = std::format("{} video {}", id, active_video.video_id);
	float active_aspect = videos::aspect_ratio(active_video);
	auto active_size =
		videos::video_size(container, animation_current(container, active_video_id, hasher("aspect"), active_aspect));
	auto active_layout = video_layout(active_video, active_size.h);
	int base_y = active_layout.y;

	float fade_step = videos::START_FADE / ui_videos.size();

	for (auto [i, ui_video] : u::enumerate(ui_videos)) {
		bool active = i == index;
		int distance = std::abs(static_cast<int>(i) - static_cast<int>(index));
		float fade = active ? 0.f : videos::START_FADE + (fade_step * (distance - 1));

		std::string video_id = std::format("{} video {}", id, ui_video.video_id);
		std::string timeline_id = std::format("{} timeline {}", id, ui_video.video_id);

		float aspect_goal = videos::aspect_ratio(ui_video);
		auto index_goal = static_cast<float>(index);

		auto size =
			videos::video_size(container, animation_current(container, video_id, hasher("aspect"), aspect_goal));

		int timeline_height = video_layout(ui_video, size.h).timeline_height;

		float offset =
			stack_offsets[i] -
			stack_position(stack_offsets, animation_current(container, video_id, hasher("offset"), index_goal));

		gfx::Rect video_rect(usable_rect.center().x - (size.w / 2), base_y + std::lround(offset), size.w, size.h);

		bool previewable = ui_video.video_info && ui_video.video_info->ffmpeg_can_decode_video;

		Element video_element(
			video_id,
			ElementType::VIDEO,
			video_rect,
			VideoElementData{
				.video = ui_video,
				.thumbnail = previewable ? thumbnails::get(ui_video.path) : std::nullopt,
				.overlay = active ? active_overlay : std::nullopt,
				.active = active,
				.fade = fade,
				.index = &index,
				.list_index = i,
				.video_count = ui_videos.size(),
				.timeline_height = timeline_height,
				.on_remove = on_remove,
			},
			render_video,
			update_video,
			remove_video,
			pause_stale_video,
			true,
			!ui_video.video_info
		);

		auto* video_elem = add_element(
			container,
			std::move(video_element),
			{
				{ hasher("main"), AnimationState(25.f) },
				{ hasher("offset"), AnimationState(OFFSET_ANIMATION_SPEED, index_goal, OFFSET_ANIMATION_SNAP) },
				{ hasher("aspect"), AnimationState(ASPECT_ANIMATION_SPEED, aspect_goal) },
				{ hasher("hover"), AnimationState(25.f) },
				{ hasher("remove_hover"), AnimationState(50.f) },
				{ hasher("info_hover"), AnimationState(40.f) },
			}
		);

		video_elem->z_index = -distance;
		video_elem->animations.at(hasher("offset")).set_goal(index_goal);
		video_elem->animations.at(hasher("aspect")).set_goal(aspect_goal);

		float duration = ui_video.video_info ? ui_video.video_info->duration : 0.f;

		Element timeline_element(
			timeline_id,
			ElementType::TIMELINE,
			videos::timeline_rect(video_rect, timeline_height),
			TimelineElementData{
				.video = ui_video,
				.player = videos::player,
				.waveform = ui_video.video_info
		                        ? videos::get_waveform(ui_video.path, videos::audio_track_count(ui_video), duration)
		                        : nullptr,
				.highlight = active && active_overlay ? active_overlay->range : std::nullopt,
				.active = active,
				.fade = fade,
				.interactive = active && video_elem->animations.at(hasher("offset")).complete,
			},
			render_timeline,
			update_timeline,
			{},
			{},
			true
		);

		auto* timeline_elem =
			add_element(container, std::move(timeline_element), videos::timeline_animations(duration));

		timeline_elem->z_index = -distance;

		if (ui_video.video_info)
			videos::init_zoom(*timeline_elem, duration);

		if (!active)
			continue;

		active_size = size;

		auto& progress_anim = timeline_elem->animations.at(hasher("progress"));

		if (switched) {
			progress_anim.current = 0.f;
			progress_anim.goal = 0.f;
		}
		else {
			videos::update_progress(*timeline_elem);
		}
	}

	reserve_space(
		container, base_y - usable_rect.y + active_size.h + videos::TIMELINE_GAP + active_layout.timeline_height
	);
}
