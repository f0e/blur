#include "videos.h"
#include "../../keys.h"
#include "../../../render/render.h"
#include "../../../fonts/icons.h"

namespace {
	constexpr int REMOVE_BUTTON_GAP = 22;
	constexpr int REMOVE_BUTTON_RADIUS = 12;
	constexpr gfx::Color REMOVE_BUTTON_COLOUR(17, 17, 17);
	constexpr gfx::Color REMOVE_BUTTON_HOVER_COLOUR(153, 40, 40);

	gfx::Point get_remove_button_pos(const gfx::Rect& rect) {
		return rect.top_right().offset(-REMOVE_BUTTON_GAP, REMOVE_BUTTON_GAP);
	}

	void render_remove_button(const ui::AnimatedElement& element, const gfx::Rect& rect, float anim) {
		float alpha = anim * element.animations.at(ui::hasher("hover")).current;
		if (alpha <= 0.f)
			return;

		float hover = element.animations.at(ui::hasher("remove_hover")).current;

		auto pos = get_remove_button_pos(rect);
		auto colour = gfx::Color::lerp(REMOVE_BUTTON_COLOUR, REMOVE_BUTTON_HOVER_COLOUR, hover).adjust_alpha(alpha);

		render::circle_filled(pos, REMOVE_BUTTON_RADIUS, colour);

		render::text(
			pos,
			gfx::Color(255, 255, 255).adjust_alpha(alpha),
			icons::CLOSE,
			fonts::icons,
			FONT_CENTERED_X | FONT_CENTERED_Y
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

	if (data.video.video_info) {
		auto inner_rect = rect.shrink(1);

		int player_w = (int)std::lround(inner_rect.w * render::framebuffer_scale);
		int player_h = (int)std::lround(inner_rect.h * render::framebuffer_scale);

		if (videos::player && videos::player->is_video_ready() && videos::is_loaded(data.video.path) &&
		    videos::player->render(player_w, player_h))
		{
			render::imgui.drawlist->AddImage(
				videos::player->get_frame_texture_for_render(),
				inner_rect.origin(),
				inner_rect.max(),
				ImVec2(0, 0),
				ImVec2(1, 1),
				IM_COL32(255, 255, 255, video_alpha)
			);
		}
		else if (data.thumbnail && data.thumbnail->texture) {
			render::image(rect, *data.thumbnail->texture, gfx::Color::white(video_alpha * 0.7f));
		}
		else if (data.thumbnail) {
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
		render_remove_button(element, rect, anim);
}

bool ui::update_video(const Container& container, AnimatedElement& element) {
	auto& data = std::get<VideoElementData>(element.element->data);

	const auto& rect = element.element->rect;
	bool mouse_in_container = container.rect.contains(keys::mouse_pos);

	bool hovered = mouse_in_container && rect.contains(keys::mouse_pos);
	bool remove_hovered = false;
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
			auto remove_button_pos = get_remove_button_pos(rect);
			float dist = std::hypot(keys::mouse_pos.x - remove_button_pos.x, keys::mouse_pos.y - remove_button_pos.y);
			remove_hovered = dist <= REMOVE_BUTTON_RADIUS;

			if (keys::is_mouse_down()) {
				keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

				if (remove_hovered) {
					if (videos::player)
						videos::player->stop();

					data.on_remove(data.video.video_id);
				}
				else if (videos::player) {
					videos::player->cycle_paused();
				}
			}
		}
	}

	// timeline scroll input waits for the stack to settle
	bool settled = element.animations.at(hasher("offset")).complete;
	bool over_timeline = settled && videos::timeline_rect(rect).contains(keys::mouse_pos);

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

	return updated;
}

void ui::remove_video(AnimatedElement& element) {
	const auto& data = std::get<VideoElementData>(element.element->data);

	gui_utils::delete_thumbnail(data.video.path);

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
