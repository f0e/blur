#include "../ui.h"
#include "../../render/render.h"
#include "../keys.h"
#include "../helpers/video.h"

namespace {
	std::unordered_map<std::string, std::unique_ptr<VideoPlayer>> video_players;
}

void ui::render_videos() {
	for (auto& [id, player] : video_players) {
		if (player) {
			player->render(100, 100);
		}
	}
}

void ui::handle_videos_event(const SDL_Event& event, bool& to_render) {
	for (auto& [id, player] : video_players) {
		if (player) {
			switch (event.type) {
				case SDL_EVENT_KEY_DOWN:
					player->handle_key_press(event.key.key);
					break;

				default:
					player->handle_mpv_event(event, to_render);
					break;
			}
		}
	}
}

void ui::render_video(const Container& container, const AnimatedElement& element) {
	const auto& video_data = std::get<VideoElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	int alpha = anim * 255;
	int stroke_alpha = anim * 125;

	render::borders(
		element.element->rect, gfx::Color(155, 155, 155, stroke_alpha), gfx::Color(80, 80, 80, stroke_alpha)
	);
}

bool ui::update_video(const Container& container, AnimatedElement& element) {
	const auto& video_data = std::get<VideoElementData>(element.element->data);

	auto video_player_it = video_players.find(video_data.video_path.string());
	if (video_player_it != video_players.end()) {
		auto& video_player = video_player_it->second;

		bool hovered = element.element->rect.contains(keys::mouse_pos) && set_hovered_element(element);

		if (hovered) {
			set_cursor(SDL_SYSTEM_CURSOR_POINTER);

			while (!event_queue.empty()) {
				auto& event = event_queue.front();

				bool blah;
				video_player->handle_mpv_event(event, blah);

				event_queue.erase(event_queue.begin());
			}
		}
	}

	return false;
}

void ui::remove_video(AnimatedElement& element) {
	const auto& video_data = std::get<VideoElementData>(element.element->data);
	auto video_player_it = video_players.find(video_data.video_path.string());
	if (video_player_it != video_players.end()) {
		video_players.erase(video_player_it);
		u::log("Removed video player for {}", video_data.video_path.string());
	}
	else {
		u::log("No video player found for {}", video_data.video_path.string());
	}
}

std::optional<ui::AnimatedElement*> ui::add_video(
	const std::string& id, Container& container, const std::filesystem::path& video_path, const gfx::Size& max_size
) {
	VideoPlayer* player = nullptr;

	auto video_player_it = video_players.find(video_path.string());
	if (video_player_it == video_players.end()) {
		try {
			auto player_ref = video_players.emplace(video_path.string(), std::make_unique<VideoPlayer>());
			player = player_ref.first->second.get();
			player->load_file(video_path.string().c_str());
			u::log("{} loaded video from {}", id, video_path.string());
		}
		catch (const std::exception& e) {
			u::log_error("{} failed to load video from {} ({})", id, video_path.string(), e.what());
			return {};
		}
	}

	float aspect_ratio = 16.0f / 9.0f; // todo: proper aspect ratio

	gfx::Rect video_rect(container.current_position, max_size);

	// maintain aspect ratio
	float target_width = video_rect.h * aspect_ratio;
	float target_height = video_rect.w / aspect_ratio;

	if (target_width <= video_rect.w) {
		video_rect.w = static_cast<int>(target_width);
	}
	else {
		video_rect.h = static_cast<int>(target_height);
	}

	if (video_rect.h > max_size.h) {
		video_rect.h = max_size.h;
		video_rect.w = static_cast<int>(max_size.h * aspect_ratio);
	}

	if (video_rect.w > max_size.w) {
		video_rect.w = max_size.w;
		video_rect.h = static_cast<int>(max_size.w / aspect_ratio);
	}

	Element element(
		id,
		ElementType::VIDEO,
		video_rect,
		VideoElementData{
			.video_path = video_path,
		},
		render_video,
		update_video,
		remove_video
	);

	return add_element(container, std::move(element), container.element_gap);
}
