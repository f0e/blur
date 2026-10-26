#include "videos.h"
#include "../../keys.h"
#include "../../../render/render.h"
#include "../../../fonts/icons.h"
#include "../../../video_info_rows.h"

namespace {
	constexpr int CORNER_BUTTON_SIZE = 26;
	constexpr int CORNER_BUTTON_GAP = 6;
	constexpr int CORNER_BUTTON_MARGIN = 10;
	constexpr float CORNER_BUTTON_ROUNDING = 6.f;

	constexpr gfx::Color CORNER_BUTTON_COLOUR(0, 0, 0, 200);
	constexpr gfx::Color CORNER_BUTTON_HOVER_COLOUR(45, 45, 45, 235);
	constexpr gfx::Color REMOVE_BUTTON_HOVER_COLOUR(153, 40, 40, 235);

	gfx::Rect get_remove_button_rect(const gfx::Rect& rect) {
		return {
			rect.x2() - CORNER_BUTTON_MARGIN - CORNER_BUTTON_SIZE,
			rect.y + CORNER_BUTTON_MARGIN,
			CORNER_BUTTON_SIZE,
			CORNER_BUTTON_SIZE,
		};
	}

	gfx::Rect get_info_button_rect(const gfx::Rect& rect) {
		auto button_rect = get_remove_button_rect(rect);
		button_rect.x -= CORNER_BUTTON_SIZE + CORNER_BUTTON_GAP;
		return button_rect;
	}

	void render_corner_button(
		const gfx::Rect& rect, const std::string& icon, gfx::Color hover_colour, float hover, float alpha
	) {
		auto background = gfx::Color::lerp(CORNER_BUTTON_COLOUR, hover_colour, hover).adjust_alpha(alpha);

		render::rounded_rect_filled(rect, background, CORNER_BUTTON_ROUNDING);
		render::rounded_rect_stroke(rect, gfx::Color::white(40 * alpha), CORNER_BUTTON_ROUNDING);

		render::text(
			rect.center(),
			gfx::Color::lerp(gfx::Color::white(190), gfx::Color::white(), hover).adjust_alpha(alpha),
			icon,
			fonts::icons,
			FONT_CENTERED_X | FONT_CENTERED_Y
		);
	}

	void render_corner_buttons(const ui::AnimatedElement& element, const gfx::Rect& rect, float anim) {
		const auto& data = std::get<ui::VideoElementData>(element.element->data);

		float alpha = anim * element.animations.at(ui::hasher("hover")).current;
		if (alpha <= 0.f)
			return;

		float remove_hover = element.animations.at(ui::hasher("remove_hover")).current;
		render_corner_button(
			get_remove_button_rect(rect), icons::CLOSE, REMOVE_BUTTON_HOVER_COLOUR, remove_hover, alpha
		);

		if (!data.video.video_info)
			return;

		auto info_rect = get_info_button_rect(rect);
		float info_hover = element.animations.at(ui::hasher("info_hover")).current;
		render_corner_button(info_rect, icons::INFO, CORNER_BUTTON_HOVER_COLOUR, info_hover, alpha);

		// lined up with the right of the pair rather than the info button
		gfx::Rect buttons_rect = info_rect;
		buttons_rect.w = get_remove_button_rect(rect).x2() - info_rect.x;

		ui::render_info_popover(
			buttons_rect,
			u::path_to_string(data.video.path.filename()),
			get_video_info_rows(*data.video.video_info, true),
			fonts::dejavu,
			alpha * info_hover,
			true
		);
	}
}

void ui::render_video(const Container& container, const AnimatedElement& element) {
	const auto& data = std::get<VideoElementData>(element.element->data);

	const auto& rect = element.element->rect;
	if (data.fade >= 1.f || !rect.on_screen())
		return;

	float anim = element.animations.at(hasher("main")).current;
	int alpha = anim * 255;
	float video_alpha = alpha * (1.f - data.fade);
	const auto* overlay_frame = data.overlay && data.overlay->frame ? &*data.overlay->frame : nullptr;

	if (data.video.video_info && !data.video.video_info->ffmpeg_can_decode_video) {
		render::text(
			rect.center(),
			gfx::Color::white(155 * anim),
			"can't preview this video",
			fonts::dejavu,
			FONT_CENTERED_X | FONT_CENTERED_Y
		);
	}
	else if (data.video.video_info) {
		bool overlay_drawn = false;
		if (overlay_frame && overlay_frame->texture) {
			render::image(rect.shrink(1), *overlay_frame->texture, gfx::Color::white(video_alpha));
			overlay_drawn = true;
		}
		else if (overlay_frame && overlay_frame->player) {
			overlay_drawn = overlay_frame->player->draw(rect.shrink(1), gfx::Color::white(video_alpha));
		}

		// the video stands in faded while the overlay's on its way
		float player_alpha = data.overlay ? video_alpha * 0.4f : video_alpha;

		bool player_drawn = overlay_drawn ||
		                    (videos::player && videos::player->is_video_ready() && videos::is_loaded(data.video.path) &&
		                     videos::player->draw(rect.shrink(1), gfx::Color::white(player_alpha)));

		if (!player_drawn && data.thumbnail && data.thumbnail->texture) {
			render::image(rect, *data.thumbnail->texture, gfx::Color::white(video_alpha * 0.7f));
		}
		else if (!player_drawn && data.thumbnail) {
			render::text(
				rect.center(),
				gfx::Color::white(155 * anim),
				data.thumbnail->error.empty() ? "failed to generate thumbnail" : data.thumbnail->error,
				fonts::dejavu,
				FONT_CENTERED_X | FONT_CENTERED_Y
			);
		}
	}
	else {
		render::spinner(rect.center(), 8.f, gfx::Color::white(50), gfx::Color::white(), 2.f, 1.f, 180.f);
	}

	render::borders(rect, gfx::Color(50, 50, 50, alpha), gfx::Color(15, 15, 15, alpha));

	if (data.active)
		render_corner_buttons(element, rect, anim);
}

bool ui::update_video(const Container& container, AnimatedElement& element) {
	auto& data = std::get<VideoElementData>(element.element->data);

	const auto& rect = element.element->rect;
	bool mouse_in_container = container.rect.contains(keys::mouse_pos);

	bool hovered = mouse_in_container && rect.contains(keys::mouse_pos);
	bool remove_hovered = false;
	bool info_hovered = false;
	bool updated = false;

	if (hovered) {
		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

		if (!data.active) {
			if (keys::is_mouse_down()) {
				keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

				*data.index = data.list_index;
				updated = true;
			}
		}
		else {
			remove_hovered = get_remove_button_rect(rect).contains(keys::mouse_pos);
			info_hovered = data.video.video_info && get_info_button_rect(rect).contains(keys::mouse_pos);

			if (keys::is_mouse_down()) {
				keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

				if (remove_hovered) {
					if (videos::player)
						videos::player->stop();

					data.on_remove(data.video.video_id);
				}
				else if (!info_hovered && videos::player) {
					videos::player->cycle_paused();
				}
			}
		}
	}

	// timeline scroll input waits for the stack to settle
	bool settled = element.animations.at(hasher("offset")).complete;
	// the track numbers and scrollbar scroll the tracks
	auto timeline_rect = videos::timeline_rect(rect, data.timeline_height);
	timeline_rect.x -= videos::TRACK_LABEL_GAP + videos::TRACK_LABEL_WIDTH;
	timeline_rect.w += videos::TRACK_LABEL_GAP + videos::TRACK_LABEL_WIDTH + videos::TRACK_SCROLLBAR_WIDTH;
	bool over_timeline = settled && timeline_rect.contains(keys::mouse_pos);

	if (data.active && mouse_in_container && !over_timeline && keys::scroll_delta != 0.f) {
		int direction = keys::scroll_delta > 0.f ? 1 : -1;

		// use the live index for fast scrolling
		auto next_index =
			(size_t)std::clamp(static_cast<int>(*data.index) + direction, 0, static_cast<int>(data.video_count) - 1);

		keys::scroll_delta = 0.f;

		if (next_index != *data.index) {
			*data.index = next_index;
			updated = true;
		}
	}

	element.animations.at(hasher("hover")).set_goal(data.active && hovered ? 1.f : 0.f);
	element.animations.at(hasher("remove_hover")).set_goal(remove_hovered ? 1.f : 0.f);
	element.animations.at(hasher("info_hover")).set_goal(info_hovered ? 1.f : 0.f);

	return updated;
}

void ui::remove_video(AnimatedElement& element) {
	const auto& data = std::get<VideoElementData>(element.element->data);

	thumbnails::remove(data.video.path);

	if (!videos::player)
		return;

	auto loaded = videos::player->get_current_file_path();
	if (!loaded || *loaded == data.video.path)
		videos::player = nullptr;
}

void ui::pause_stale_video(AnimatedElement& element) {
	const auto& data = std::get<VideoElementData>(element.element->data);

	if (videos::player && videos::is_loaded(data.video.path))
		videos::player->set_paused(true);
}
