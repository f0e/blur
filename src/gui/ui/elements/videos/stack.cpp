#include "videos.h"

constexpr float OFFSET_ANIMATION_SPEED = 25.f;
constexpr float OFFSET_ANIMATION_SNAP = 0.0005f;
constexpr float ASPECT_ANIMATION_SPEED = 15.f;
constexpr float ZOOM_ANIMATION_SPEED = 30.f;

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

gfx::Rect ui::videos::timeline_rect(const gfx::Rect& video_rect) {
	return gfx::Rect{
		gfx::Point(video_rect.x, video_rect.y2()).offset_y(TIMELINE_GAP),
		gfx::Size(video_rect.w, TIMELINE_HEIGHT),
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
	const std::vector<UIVideo>& ui_videos,
	size_t& index,
	float& start,
	float& end,
	float& volume,
	bool hardware_decoding,
	bool trim_disabled,
	const std::function<void(size_t video_id)>& on_remove
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

	if (!videos::is_loaded(active_video.path)) {
		videos::player->load_file(active_video.path);
		videos::player->set_paused(true);
	}

	if (active_video.start && active_video.end) {
		videos::player->set_start(*active_video.start);
		videos::player->set_end(*active_video.end);
	}

	std::vector<float> stack_offsets;
	stack_offsets.reserve(ui_videos.size());

	float stack_offset = 0.f;
	for (const auto& ui_video : ui_videos) {
		stack_offsets.push_back(stack_offset);
		stack_offset += videos::video_size(container, videos::aspect_ratio(ui_video)).h + videos::TIMELINE_GAP +
		                videos::TIMELINE_HEIGHT + videos::VIDEO_GAP;
	}

	auto usable_rect = container.get_usable_rect();
	std::string active_video_id = std::format("{} video {}", id, active_video.video_id);
	float active_aspect = videos::aspect_ratio(active_video);
	auto active_size =
		videos::video_size(container, animation_current(container, active_video_id, hasher("aspect"), active_aspect));
	int base_y = usable_rect.center().y - active_size.h / 2;

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

		float offset =
			stack_offsets[i] -
			stack_position(stack_offsets, animation_current(container, video_id, hasher("offset"), index_goal));

		gfx::Rect video_rect(usable_rect.center().x - (size.w / 2), base_y + std::lround(offset), size.w, size.h);

		Element video_element(
			video_id,
			ElementType::VIDEO,
			video_rect,
			VideoElementData{
				.video = ui_video,
				.thumbnail = ui_video.video_info ? gui_utils::get_thumbnail(ui_video.path) : std::nullopt,
				.active = active,
				.fade = fade,
				.index = &index,
				.list_index = i,
				.video_count = ui_videos.size(),
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
			}
		);

		video_elem->z_index = -distance;
		video_elem->animations.at(hasher("offset")).set_goal(index_goal);
		video_elem->animations.at(hasher("aspect")).set_goal(aspect_goal);

		float duration = ui_video.video_info ? ui_video.video_info->duration : 0.f;

		Element timeline_element(
			timeline_id,
			ElementType::TIMELINE,
			videos::timeline_rect(video_rect),
			TimelineElementData{
				.video = ui_video,
				.waveform = ui_video.video_info ? videos::get_waveform(ui_video.path, duration) : nullptr,
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

		auto* timeline_elem = add_element(
			container,
			std::move(timeline_element),
			{
				{ hasher("main"), AnimationState(25.f) },
				{ hasher("progress"), AnimationState(70.f) },
				{ hasher("seeking"), AnimationState(70.f) },
				{ hasher("seek"), AnimationState(70.f) },
				{ hasher("left_grab"), AnimationState(150.f) },
				{ hasher("right_grab"), AnimationState(150.f) },
				{ hasher("zoom_start"), AnimationState(ZOOM_ANIMATION_SPEED, 0.f) },
				{ hasher("zoom_end"), AnimationState(ZOOM_ANIMATION_SPEED, duration) },
			}
		);

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

	reserve_space(container, base_y - usable_rect.y + active_size.h + videos::TIMELINE_GAP + videos::TIMELINE_HEIGHT);
}
