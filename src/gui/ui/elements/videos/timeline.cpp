#include "videos.h"
#include "../../helpers/frame_snap.h"
#include "../../keys.h"
#include "../../../render/render.h"

namespace {
	constexpr int GRABS_THICKNESS = 1;
	constexpr int GRABS_LENGTH = 5;
	constexpr gfx::Color GRABS_COLOR(175, 175, 175);
	constexpr gfx::Color GRABS_ACTIVE_COLOR(100, 100, 100);
	constexpr float DISABLED_GRABS_ALPHA = 0.35f;
	constexpr gfx::Size GRAB_CLICK_EXPANSION(15, 5);
	constexpr int PLAYHEAD_WIDTH = 2;

	constexpr float ZOOM_SPEED = 1.4f;
	constexpr float MIN_ZOOM_SECS = 0.6f;
	constexpr float ZOOM_ANIMATION_SPEED = 30.f;

	// shared timeline drag state
	struct {
		bool grabbing = false;
		bool moving = false;
		std::optional<int> start_mouse_x;
		std::optional<int> last_pan_x;
		std::optional<float> last_seek;
	} drag;

	struct GrabRects {
		gfx::Rect left;
		gfx::Rect right;
	};

	// a filled rect rather than a line, which imgui offsets by half a pixel and so falls short of the top
	gfx::Rect playhead_rect(const gfx::Rect& rect, float local) {
		int x = rect.x + static_cast<int>(local * rect.w);
		return { x - (PLAYHEAD_WIDTH / 2), rect.y, PLAYHEAD_WIDTH, rect.h };
	}

	// around the whole video when there's no range
	GrabRects get_grab_rects(
		const ui::UIVideo& video, const gfx::Rect& rect, float visible_start, float visible_range
	) {
		float start = video.start != nullptr ? *video.start : 0.f;
		float end = video.end != nullptr ? *video.end : 1.f;

		float left_t = (start - visible_start) / visible_range;
		float right_t = (end - visible_start) / visible_range;

		auto left = rect;
		left.x = rect.x + static_cast<int>(left_t * rect.w);
		left.w = GRABS_LENGTH;

		auto right = rect;
		right.x = rect.x + static_cast<int>(right_t * rect.w) - GRABS_LENGTH;
		right.w = GRABS_LENGTH;

		return { .left = left, .right = right };
	}

	// prefer the player's duration to metadata
	float get_duration(const ui::TimelineElementData& data) {
		if (data.player && data.player->get_duration())
			return static_cast<float>(*data.player->get_duration());

		return data.video.video_info->duration;
	}

	float get_fps(const ui::TimelineElementData& data) {
		if (data.player && data.player->get_fps())
			return static_cast<float>(*data.player->get_fps());

		return data.video.video_info->fps_num / (float)data.video.video_info->fps_den;
	}
}

std::unordered_map<size_t, ui::AnimationState> ui::videos::timeline_animations(float duration) {
	return {
		{ hasher("main"), AnimationState(25.f) },
		{ hasher("progress"), AnimationState(70.f) },
		{ hasher("seeking"), AnimationState(70.f) },
		{ hasher("seek"), AnimationState(70.f) },
		{ hasher("left_grab"), AnimationState(150.f) },
		{ hasher("right_grab"), AnimationState(150.f) },
		{ hasher("zoom_start"), AnimationState(ZOOM_ANIMATION_SPEED, 0.f) },
		{ hasher("zoom_end"), AnimationState(ZOOM_ANIMATION_SPEED, duration) },
	};
}

void ui::videos::init_zoom(AnimatedElement& timeline, float duration) {
	auto& zoom_end = timeline.animations.at(hasher("zoom_end"));
	if (zoom_end.goal > 0.f)
		return;

	zoom_end.current = duration;
	zoom_end.goal = duration;
}

void ui::videos::update_progress(AnimatedElement& timeline) {
	const auto& data = std::get<TimelineElementData>(timeline.element->data);

	if (drag.grabbing || !data.player)
		return;

	if (data.player->is_seeking() || data.player->get_queued_seek())
		return;

	auto progress_percent = data.player->get_percent_pos();
	if (!progress_percent)
		return;

	timeline.animations.at(hasher("progress")).set_goal(*progress_percent / 100.f);
}

ui::AnimatedElement* ui::add_timeline(
	const std::string& id, Container& container, TimelineElementData data, int height, std::optional<int> width
) {
	float duration = data.video.video_info ? data.video.video_info->duration : 0.f;
	bool has_info = data.video.video_info.has_value();

	Element element(
		id,
		ElementType::TIMELINE,
		gfx::Rect(container.current_position, gfx::Size(width.value_or(container.get_usable_rect().w), height)),
		std::move(data),
		render_timeline,
		update_timeline
	);

	auto* timeline =
		add_element(container, std::move(element), container.element_gap, videos::timeline_animations(duration));

	if (has_info) {
		videos::init_zoom(*timeline, duration);
		videos::update_progress(*timeline);
	}

	return timeline;
}

void ui::render_timeline(const Container& container, const AnimatedElement& element) {
	const auto& data = std::get<TimelineElementData>(element.element->data);

	if (data.fade >= 1.f || !data.video.video_info)
		return;

	bool has_range = data.video.start != nullptr && data.video.end != nullptr;

	auto rect = element.element->rect;

	float anim = element.animations.at(hasher("main")).current * (1.f - data.fade);
	float left_grab = element.animations.at(hasher("left_grab")).current;
	float right_grab = element.animations.at(hasher("right_grab")).current;

	float duration = data.video.video_info->duration;
	float zoom_start = element.animations.at(hasher("zoom_start")).current;
	float zoom_end = element.animations.at(hasher("zoom_end")).current;
	if (zoom_end <= zoom_start) {
		zoom_start = 0.f;
		zoom_end = duration;
	}

	float visible_start = zoom_start / duration;
	float visible_range = (zoom_end - zoom_start) / duration;
	float visible_end = visible_start + visible_range;

	auto grab_rects = get_grab_rects(data.video, rect, visible_start, visible_range);

	constexpr int STROKE_ALPHA = 125;
	render::push_clip_rect(container.rect);
	render::rect_filled(rect, gfx::Color::black(STROKE_ALPHA * anim));
	render::rect_stroke(rect, gfx::Color(155, 155, 155, STROKE_ALPHA * anim));
	// a pixel spare past the grabs, since the scissor rect gets truncated to framebuffer pixels
	render::push_clip_rect(rect.expand(GRABS_THICKNESS + 1), true);

	if (has_range) {
		float grabs_alpha = anim * (data.video.trim_disabled ? DISABLED_GRABS_ALPHA : 1.f);
		render::rect_side(
			grab_rects.left,
			gfx::Color::lerp(GRABS_COLOR, GRABS_ACTIVE_COLOR, left_grab).adjust_alpha(grabs_alpha),
			render::RectSide::LEFT,
			GRABS_THICKNESS
		);
		render::rect_side(
			grab_rects.right,
			gfx::Color::lerp(GRABS_COLOR, GRABS_ACTIVE_COLOR, right_grab).adjust_alpha(grabs_alpha),
			render::RectSide::RIGHT,
			GRABS_THICKNESS
		);
	}

	rect = rect.shrink(1);

	if (data.highlight && data.highlight->second >= visible_start && data.highlight->first <= visible_end) {
		float left = std::clamp((data.highlight->first - visible_start) / visible_range, 0.f, 1.f);
		float right = std::clamp((data.highlight->second - visible_start) / visible_range, 0.f, 1.f);

		gfx::Rect highlight_rect = rect;
		highlight_rect.x = rect.x + static_cast<int>(left * rect.w);
		highlight_rect.w = std::max(static_cast<int>(right * rect.w) - static_cast<int>(left * rect.w), 1);

		render::rect_filled(highlight_rect, gfx::Color::white(40 * anim));
	}

	if (data.waveform) {
		auto active_rect = rect;
		active_rect.x = grab_rects.left.x;
		active_rect.w = grab_rects.right.x2() - active_rect.x;

		render::waveform(
			rect,
			active_rect,
			gfx::Color(120, 120, 120, 255 * anim),
			data.waveform->samples,
			data.waveform->max_sample,
			visible_start,
			visible_end
		);
	}

	if (data.active) {
		float zoom_range = zoom_end - zoom_start;

		float progress = element.animations.at(hasher("progress")).current;
		float progress_local = ((progress * duration) - zoom_start) / zoom_range;

		float progress_anim = anim * (1.f - left_grab) * (1.f - right_grab);
		render::rect_filled(playhead_rect(rect, progress_local), gfx::Color::white(progress_anim * 255));

		float seeking = element.animations.at(hasher("seeking")).current;
		if (seeking > 0.f) {
			float seek = element.animations.at(hasher("seek")).current;
			float seek_local = std::clamp(((seek * duration) - zoom_start) / zoom_range, 0.f, 1.f);

			render::rect_filled(playhead_rect(rect, seek_local), gfx::Color::white(75 * anim * seeking));
		}
	}

	render::pop_clip_rect();
	render::pop_clip_rect();
}

bool ui::update_timeline(const Container& container, AnimatedElement& element) {
	auto& data = std::get<TimelineElementData>(element.element->data);

	if (!data.active || !data.video.video_info)
		return false;

	bool has_range = data.video.start != nullptr && data.video.end != nullptr;

	const auto& rect = element.element->rect;

	float duration = get_duration(data);
	float fps = get_fps(data);

	auto& zoom_start_anim = element.animations.at(hasher("zoom_start"));
	auto& zoom_end_anim = element.animations.at(hasher("zoom_end"));

	float zoom_start = zoom_start_anim.current;
	float zoom_end = zoom_end_anim.current;
	float zoom_range = zoom_end - zoom_start;

	if (zoom_range <= 0.f) {
		zoom_range = duration;
		zoom_end = zoom_start + zoom_range;
	}

	float visible_start = zoom_start / duration;
	float visible_range = zoom_range / duration;

	auto grab_rects = get_grab_rects(data.video, rect, visible_start, visible_range);

	bool updated = false;

	struct Grab {
		gfx::Rect rect;
		AnimationState& anim; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) only lives for this call
		float* value = nullptr;
		float* min = nullptr;
		float* max = nullptr;
		bool is_start = false;
		bool hovered = false;
		bool active = false;
	};

	std::array grabs = {
		Grab{
			.rect = grab_rects.left.expand(GRAB_CLICK_EXPANSION),
			.anim = element.animations.at(hasher("left_grab")),
			.value = data.video.start,
			.min = nullptr,
			.max = data.video.end,
			.is_start = true,
		},
		Grab{
			.rect = grab_rects.right.expand(GRAB_CLICK_EXPANSION),
			.anim = element.animations.at(hasher("right_grab")),
			.value = data.video.end,
			.min = data.video.start,
			.max = nullptr,
			.is_start = false,
		},
	};

	bool grabbing = false;

	for (auto [i, grab] : u::enumerate(grabs)) {
		if (!has_range || data.video.trim_disabled) {
			grab.anim.set_goal(0.f);
			continue;
		}

		std::string action = "grab_" + std::to_string(i);

		grab.hovered = grab.rect.contains(keys::mouse_pos) && set_hovered_element(element);

		if (grab.hovered) {
			set_cursor(SDL_SYSTEM_CURSOR_POINTER);
			if (!get_active_element() && keys::is_mouse_down())
				set_active_element(element, action);
		}

		if (is_active_element(element, action)) {
			if (keys::is_mouse_down()) {
				grab.active = true;
				grab.anim.set_goal(1.f);

				if (!drag.start_mouse_x) {
					drag.start_mouse_x = keys::mouse_pos.x;

					if (data.player)
						data.player->seek(*grab.value * duration, true);
				}
				else if (drag.moving || keys::mouse_pos.x != drag.start_mouse_x) {
					drag.moving = true;

					float local_percent = std::clamp(static_cast<float>(keys::mouse_pos.x - rect.x) / rect.w, 0.f, 1.f);

					float percent = visible_start + (local_percent * visible_range);
					percent = video::frame_snap::snap_percent(percent, duration, fps);
					percent = std::clamp(percent, grab.min ? *grab.min : 0.f, grab.max ? *grab.max : 1.f);

					if (percent != *grab.value) {
						*grab.value = percent;

						if (data.player) {
							if (grab.is_start)
								data.player->set_start(percent);
							else
								data.player->set_end(percent);

							data.player->seek(percent * duration, true);
						}
					}

					auto& grab_progress_anim = element.animations.at(hasher("progress"));
					grab_progress_anim.current = percent;
					grab_progress_anim.set_goal(percent);
				}
			}
			else {
				drag.moving = false;
				drag.start_mouse_x = {};

				reset_active_element();
			}
		}

		if (!grab.active)
			grab.anim.set_goal(grab.hovered ? 0.5f : 0.f);

		updated |= grab.active;
		grabbing |= grab.active;
	}

	drag.grabbing = grabbing;

	bool hovered = !updated && rect.contains(keys::mouse_pos) && set_hovered_element(element);

	auto pan_by = [&](float timeline_delta) {
		float new_start = zoom_start_anim.goal - timeline_delta;
		float new_end = zoom_end_anim.goal - timeline_delta;

		if (new_start < 0.f) {
			new_end += -new_start;
			new_start = 0.f;
		}
		if (new_end > duration) {
			new_start -= new_end - duration;
			new_end = duration;
		}

		zoom_start_anim.set_goal(new_start);
		zoom_end_anim.set_goal(new_end);
		updated = true;
	};

	// timeline scrolling waits for the stack to settle
	if (data.interactive && rect.contains(keys::mouse_pos)) {
		if (keys::scroll_x_delta != 0.f) {
			pan_by((keys::scroll_x_delta / 30.f) * zoom_range);

			keys::scroll_x_delta = 0.f;
		}

		if (keys::scroll_delta != 0.f) {
			float current_start = zoom_start_anim.goal;
			float current_range = zoom_end_anim.goal - current_start;

			float new_range = std::clamp(current_range * powf(ZOOM_SPEED, keys::scroll_delta), MIN_ZOOM_SECS, duration);

			float mouse_local = std::clamp(float(keys::mouse_pos.x - rect.x) / rect.w, 0.f, 1.f);
			float mouse_timeline = current_start + (mouse_local * current_range);

			float new_start = mouse_timeline - (mouse_local * new_range);
			float new_end = new_start + new_range;

			if (new_start < 0.f) {
				new_start = 0.f;
				new_end = new_start + new_range;
			}
			if (new_end > duration) {
				new_end = duration;
				new_start = new_end - new_range;
			}

			zoom_start_anim.set_goal(new_start);
			zoom_end_anim.set_goal(new_end);

			updated = true;

			keys::scroll_delta = 0.f;
		}
	}

	if (hovered) {
		if (keys::is_mouse_down(SDL_BUTTON_RIGHT)) {
			if (!get_active_element())
				set_active_element(element, "timeline_pan");
		}
		else if (keys::is_mouse_down()) {
			set_active_element(element, "timeline_seek");
		}
	}

	if (is_active_element(element, "timeline_pan")) {
		if (keys::is_mouse_down(SDL_BUTTON_RIGHT)) {
			if (drag.last_pan_x)
				pan_by(((float)(keys::mouse_pos.x - *drag.last_pan_x) / rect.w) * zoom_range);

			drag.last_pan_x = keys::mouse_pos.x;
		}
		else {
			reset_active_element();
			drag.last_pan_x = {};
		}
	}

	auto& progress_anim = element.animations.at(hasher("progress"));
	auto& seeking_anim = element.animations.at(hasher("seeking"));

	if (is_active_element(element, "timeline_seek")) {
		if (keys::is_mouse_down()) {
			seeking_anim.set_goal(1.f);

			float local_percent = std::clamp(static_cast<float>(keys::mouse_pos.x - rect.x) / rect.w, 0.f, 1.f);

			float time = video::frame_snap::snap_time(zoom_start + (local_percent * zoom_range), fps);
			time = std::clamp(time, 0.f, duration);
			float percent = time / duration;

			if (data.player && time != drag.last_seek) {
				data.player->seek(time, true);
				drag.last_seek = time;
			}

			progress_anim.set_goal(percent);
			element.animations.at(hasher("seek")).set_goal(percent);

			updated = true;
		}
		else {
			drag.last_seek = {};

			reset_active_element();
		}
	}

	float current_percent = progress_anim.goal;

	if (has_range && (keys::is_key_pressed(SDL_SCANCODE_LEFTBRACKET) || keys::is_key_pressed(SDL_SCANCODE_G))) {
		*data.video.start = std::clamp(current_percent, 0.f, 1.f);

		if (*data.video.end < *data.video.start) {
			*data.video.end = 1.f;
		}

		updated = true;
	}

	if (has_range && (keys::is_key_pressed(SDL_SCANCODE_RIGHTBRACKET) || keys::is_key_pressed(SDL_SCANCODE_H))) {
		*data.video.end = std::clamp(current_percent, 0.f, 1.f);

		if (*data.video.start > *data.video.end) {
			*data.video.start = 0.f;
		}

		updated = true;
	}

	if (data.player && !data.player->get_queued_seek())
		seeking_anim.set_goal(0.f);

	return updated;
}
