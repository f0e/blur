#include "../ui.h"
#include "../../render/render.h"
#include "../keys.h"
#include "../helpers/video.h"
#include "../../sdl.h"
#include "frame_snap.h"
#include "common/waveforms.h"

// videos
constexpr gfx::Size LOADER_SIZE(20, 20);
constexpr gfx::Size LOADER_PAD(5, 5);
constexpr float START_FADE = 0.5f;
constexpr int VIDEO_GAP = 30;

// track
constexpr int TRACK_GAP = 10;
constexpr int TRACK_SIDES_GAP = 45; // todo: use container available width shit instead of this
constexpr int MIN_TRACK_WIDTH = sdl::MINIMUM_WINDOW_SIZE.w - (TRACK_SIDES_GAP * 2);

constexpr int TRACK_HEIGHT = 40;
constexpr int GRABS_THICKNESS = 1;
constexpr int GRABS_LENGTH = 5;
constexpr gfx::Color GRABS_COLOR(175, 175, 175);
constexpr gfx::Color GRABS_ACTIVE_COLOR(100, 100, 100);
constexpr gfx::Size GRAB_CLICK_EXPANSION(15, 5);

constexpr float TRACK_ZOOM_SPEED = 1.4f;
constexpr float TRACK_MAX_ZOOM_SECS = 0.6f;
constexpr size_t WAVEFORM_SAMPLES_PER_SEC = 80;

namespace {
	std::shared_ptr<VideoPlayer> video_player; // TODO: should probably be in VideoData, but there's only one of those
	                                           // showing at a time so i don't care

	gfx::Size fit_size(const ui::Container& container, const ui::UIVideo& video) {
		gfx::Size size = LOADER_SIZE;

		if (!video.video_info)
			return size;

		float aspect_ratio = static_cast<float>(video.video_info->width) / static_cast<float>(video.video_info->height);

		gfx::Size max_size(container.get_usable_rect().w, container.get_usable_rect().h / 1.5f);
		size = max_size;

		// maintain aspect ratio while fitting within max_size
		float target_width = size.h * aspect_ratio;
		float target_height = size.w / aspect_ratio;

		if (target_width <= max_size.w) {
			size.w = static_cast<int>(target_width);
		}
		else {
			size.h = static_cast<int>(target_height);
		}

		// ensure we don't exceed max dimensions
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

	std::vector<gfx::Rect> get_video_rects(const ui::AnimatedElement& element, const gfx::Rect& rect) {
		const auto& video_data = std::get<ui::VideoElementData>(element.element->data);

		std::vector<gfx::Rect> rects;

		float offset = 0.f;

		for (auto [i, video] : u::enumerate(video_data.videos)) {
			gfx::Rect player_rect(rect.origin().offset_x(offset), video.size);

			rects.push_back(player_rect);

			offset += player_rect.w + VIDEO_GAP;
		}

		return rects;
	}

	// track
	std::unordered_map<std::string, ui::VideoElementData::StoredWaveform> waveforms;

	tl::expected<ui::VideoElementData::StoredWaveform*, std::string> get_waveform(
		const std::filesystem::path& path, const std::optional<double>& duration
	) {
		if (!duration)
			return tl::unexpected("no duration provided");

		auto key = path.string();

		try {
			auto it = waveforms.find(key);

			if (it == waveforms.end()) {
				auto waveform_res = waveforms::get_waveform(key, *duration * WAVEFORM_SAMPLES_PER_SEC);
				if (!waveform_res) // still generating/error
					return tl::unexpected("waveform not ready");

				auto samples = *waveform_res;

				int16_t max_sample = 1;
				for (const auto& sample : samples) {
					max_sample = std::max<int>(std::abs(sample), max_sample);
				}

				auto insert_result = waveforms.insert(
					{
						key,
						{
							.samples = samples,
							.max_sample = max_sample,
						},
					}
				);
				it = insert_result.first;
			}

			return &it->second;
		}
		catch (const std::exception& e) {
			u::log_error("failed to load video from {} ({})", key, e.what());
			return tl::unexpected("failed to load video");
		}
	}

	struct GrabRects {
		gfx::Rect left;
		gfx::Rect right;
	};

	GrabRects get_grab_rects(
		const ui::AnimatedElement& element, const gfx::Rect& full_rect, float zoom_start, float zoom_end
	) {
		auto& video_data = std::get<ui::VideoElementData>(element.element->data);

		float left_t = (*video_data.start - zoom_start) / zoom_end;
		float right_t = (*video_data.end - zoom_start) / zoom_end;

		auto left_grab_rect = full_rect;
		left_grab_rect.x = full_rect.x + static_cast<int>(left_t * full_rect.w);
		left_grab_rect.w = GRABS_LENGTH;

		auto right_grab_rect = full_rect;
		right_grab_rect.x = full_rect.x + static_cast<int>(right_t * full_rect.w) - GRABS_LENGTH;
		right_grab_rect.w = GRABS_LENGTH;

		return { .left = left_grab_rect, .right = right_grab_rect };
	}

	// both
	gfx::Rect get_video_rect(const gfx::Point& origin, const gfx::Size& video_size, float offset) {
		return gfx::Rect{ origin.offset_x(offset), video_size };
	}

	gfx::Rect get_track_rect(const gfx::Point& origin, const gfx::Size& video_size) {
		return gfx::Rect{
			gfx::Point(origin.x, origin.y + video_size.h).offset_y(TRACK_GAP),
			gfx::Size(std::max(MIN_TRACK_WIDTH, video_size.w), TRACK_HEIGHT),
		};
	}

	// cancer cause have to pointer
	const ui::VideoElementData::Video* get_active_video(const ui::AnimatedElement& element) {
		const auto& video_data = std::get<ui::VideoElementData>(element.element->data);

		if (video_data.videos.size() == 0 || *video_data.index < 0 || *video_data.index >= video_data.videos.size())
			return nullptr;

		auto& video = video_data.videos[*video_data.index];

		return &video;
	}

	std::optional<float> get_video_offset(const ui::Element& element) {
		const auto& video_data = std::get<ui::VideoElementData>(element.data);

		if (*video_data.index < 0 || *video_data.index >= video_data.videos.size())
			return {};

		const auto& active_video = video_data.videos[*video_data.index];

		auto track_rect = get_track_rect(element.rect.origin(), active_video.size);

		float offset = 0;
		int last_width = 0;
		for (int i = 0; i <= *video_data.index; ++i) {
			int width = video_data.videos[i].size.w;

			if (i != *video_data.index)
				offset -= width + VIDEO_GAP; // shift left by widths + spacing
			else
				offset += ((float)track_rect.w / 2) - ((float)width / 2);

			last_width = width;
		}

		return offset;
	}

	void update_progress(ui::AnimatedElement& element) {
		auto& video_data = std::get<ui::VideoElementData>(element.element->data);
		if (video_data.handle_info.grabbing)
			return;

		const auto* active_video = get_active_video(element);
		if (!active_video || !video_player)
			return;

		if (video_player->is_seeking() || video_player->get_queued_seek())
			return;

		auto progress_percent = video_player->get_percent_pos();
		if (!progress_percent)
			return;

		float target_percent = *progress_percent / 100.f;

		target_percent =
			video::frame_snap::snap_percent(target_percent, *video_player->get_duration(), *video_player->get_fps());

		auto& progress_anim = element.animations.at(ui::hasher("progress"));

		progress_anim.set_goal(*progress_percent / 100.f);
	}

	void init_zoom(ui::AnimatedElement& element) {
		auto& video_data = std::get<ui::VideoElementData>(element.element->data);

		auto track_zoom_start_hash = ui::hasher("track_zoom_start");
		auto track_zoom_end_hash = ui::hasher("track_zoom_end");

		const auto& active_video = video_data.videos[*video_data.index];

		// deinitialise zoom on video switch
		if (element.animations.contains(track_zoom_end_hash)) {
			if (!video_data.last_active_video || *video_data.last_active_video != active_video.data.path) {
				element.animations.erase(track_zoom_start_hash);
				element.animations.erase(track_zoom_end_hash);

				DEBUG_LOG("switched video, deinitialised zoom");
			}
		}

		// initialise zoom
		if (!element.animations.contains(track_zoom_end_hash) && active_video.data.video_info) {
			element.animations.emplace(track_zoom_start_hash, ui::AnimationState(30.f, 0.f));
			element.animations.emplace(
				track_zoom_end_hash, ui::AnimationState(30.f, active_video.data.video_info->duration)
			);

			video_data.last_active_video = active_video.data.path;

			DEBUG_LOG("initialised zoom");
		}
	}
}

void ui::handle_videos_event(const SDL_Event& event, bool& to_render) {
	if (!video_player)
		return;

	switch (event.type) {
		case SDL_EVENT_KEY_DOWN:
			video_player->handle_key_press(event.key.key);
			break;

		default:
			video_player->handle_mpv_event(event, to_render, true);
			break;
	}
}

void render_videos_actual(const ui::Container& container, const ui::AnimatedElement& element) {
	const auto& video_data = std::get<ui::VideoElementData>(element.element->data);

	const auto* active_video = get_active_video(element);
	if (!active_video)
		return;

	float anim = element.animations.at(ui::hasher("main")).current;
	float offset = element.animations.at(ui::hasher("video_offset")).current;

	int alpha = anim * 255;

	float fade_step = START_FADE / video_data.videos.size();

	auto rect = get_video_rect(element.element->rect.origin(), active_video->size, offset);

	std::vector<gfx::Rect> rects = get_video_rects(element, rect);

	for (auto [i, video] : u::enumerate(video_data.videos)) {
		float fade = i == *video_data.index ? 0.f : START_FADE + (fade_step * i);
		if (fade >= 1.f)
			continue;

		auto video_rect = rects[i];
		if (!video_rect.on_screen())
			continue;

		auto inner_rect = video_rect.shrink(1);
		float player_alpha = alpha * (1.f - fade);

		gfx::Rect loader_rect = inner_rect.shrink(LOADER_PAD, true);
		auto loader_colour = gfx::Color::white(155 * anim);

		if (video_player && video_player->is_video_ready() && video_player->get_current_file_path() &&
		    *video_player->get_current_file_path() == video.data.path &&
		    video_player->render(inner_rect.w, inner_rect.h))
		{
			// TODO: render::image
			render::imgui.drawlist->AddImage(
				video_player->get_frame_texture_for_render(),
				inner_rect.origin(),
				inner_rect.max(),
				ImVec2(0, 0),
				ImVec2(1, 1),
				IM_COL32(255, 255, 255, player_alpha) // apply alpha for fade animations
			);
		}
		else if (video.thumbnail && video.thumbnail->texture) {
			render::image(video_rect, *video.thumbnail->texture, gfx::Color::white(player_alpha * 0.7f));
		}
		else if (video.thumbnail) {
			render::text(
				loader_rect.center(),
				loader_colour,
				video.thumbnail->error.empty() ? "failed to generate thumbnail" : video.thumbnail->error,
				fonts::dejavu,
				FONT_CENTERED_X | FONT_CENTERED_Y
			);
		}
		else {
			render::loader(loader_rect, loader_colour);
		}

		render::borders(video_rect, gfx::Color(50, 50, 50, alpha), gfx::Color(15, 15, 15, alpha));
	}
}

void render_track(const ui::Container& container, const ui::AnimatedElement& element) {
	const auto& video_data = std::get<ui::VideoElementData>(element.element->data);

	const auto* active_video = get_active_video(element);
	if (!active_video->data.video_info)
		return;

	if (!element.animations.contains(ui::hasher("track_zoom_end")))
		return;

	auto rect = get_track_rect(element.element->rect.origin(), active_video->size);

	float anim = element.animations.at(ui::hasher("main")).current;
	float progress = element.animations.at(ui::hasher("progress")).current;
	float seeking = element.animations.at(ui::hasher("seeking")).current;
	float seek = element.animations.at(ui::hasher("seek")).current;
	float left_grab = element.animations.at(ui::hasher("left_grab")).current;
	float right_grab = element.animations.at(ui::hasher("right_grab")).current;

	int stroke_alpha = 125;

	render::rect_filled(rect, gfx::Color::black(stroke_alpha * anim));
	render::rect_stroke(rect, gfx::Color(155, 155, 155, stroke_alpha * anim));

	// read zoom from new start/end animations
	float track_zoom_start = element.animations.at(ui::hasher("track_zoom_start")).current;
	float track_zoom_end = element.animations.at(ui::hasher("track_zoom_end")).current;

	// convert to normalized coordinates
	float visible_start = track_zoom_start / (active_video->data.video_info->duration);
	float visible_range = (track_zoom_end - track_zoom_start) / (active_video->data.video_info->duration);
	float visible_end = visible_start + visible_range;

	// compute grab rects in timeline coordinates
	auto grab_rects = get_grab_rects(element, rect, visible_start, visible_range);

	render::push_clip_rect(rect.expand(1));

	render::rect_side(
		grab_rects.left,
		gfx::Color::lerp(GRABS_COLOR, GRABS_ACTIVE_COLOR, left_grab).adjust_alpha(anim),
		render::RectSide::LEFT,
		GRABS_THICKNESS
	);

	render::rect_side(
		grab_rects.right,
		gfx::Color::lerp(GRABS_COLOR, GRABS_ACTIVE_COLOR, right_grab).adjust_alpha(anim),
		render::RectSide::RIGHT,
		GRABS_THICKNESS
	);

	// dont show progress when grabbing, its implied that you're at where you're grabbing
	float progress_anim = anim;
	progress_anim *= (1.f - left_grab);
	progress_anim *= (1.f - right_grab);

	rect = rect.shrink(1);

	if (active_video->waveform) {
		auto active_rect = rect;
		active_rect.x = grab_rects.left.x;
		active_rect.w = grab_rects.right.x2() - active_rect.x;

		render::waveform(
			rect,
			active_rect,
			gfx::Color(120, 120, 120, 255 * anim),
			(*active_video->waveform)->samples,
			(*active_video->waveform)->max_sample,
			visible_start,
			visible_end
		);
	}

	// Convert progress from normalized (0-1) to visible window coordinates
	float progress_timeline = progress * (active_video->data.video_info->duration);
	float progress_local = (progress_timeline - track_zoom_start) / (track_zoom_end - track_zoom_start);

	gfx::Point progress_point = rect.origin();
	progress_point.x = rect.x + static_cast<int>(progress_local * rect.w);

	render::line(progress_point, progress_point.offset_y(rect.h), gfx::Color::white(anim * 255), false, 2.f);

	if (seeking > 0.f) {
		// Convert seek position from normalized to visible window coordinates
		float seek_timeline = seek * (active_video->data.video_info->duration);
		float seek_local = (seek_timeline - track_zoom_start) / (track_zoom_end - track_zoom_start);
		seek_local = std::clamp(seek_local, 0.f, 1.f);

		gfx::Point seek_point = rect.origin();
		seek_point.x = rect.x + static_cast<int>(seek_local * rect.w);

		render::line(seek_point, seek_point.offset_y(rect.h), gfx::Color::white(75 * anim * seeking), false, 2.f);
	}

	render::pop_clip_rect();
}

void ui::render_videos(const Container& container, const AnimatedElement& element) {
	const auto& video_data = std::get<ui::VideoElementData>(element.element->data);

	render_videos_actual(container, element);
	render_track(container, element);
}

bool update_track(const ui::Container& container, ui::AnimatedElement& element) {
	auto& video_data = std::get<ui::VideoElementData>(element.element->data);

	const auto* active_video = get_active_video(element);
	if (!active_video || !active_video->data.video_info)
		return false;

	auto active_video_duration = video_player && video_player->get_duration() ? *video_player->get_duration()
	                                                                          : active_video->data.video_info->duration;
	auto active_video_fps = video_player && video_player->get_fps() ? *video_player->get_fps()
	                                                                : active_video->data.video_info->fps_num /
	                                                                      (float)active_video->data.video_info->fps_den;

	if (!element.animations.contains(ui::hasher("track_zoom_end")))
		return false;

	bool updated = false;

	auto rect = get_track_rect(element.element->rect.origin(), active_video->size);

	// Use the new zoom animation states (start/end instead of scale/offset)
	auto& track_zoom_start_anim = element.animations.at(ui::hasher("track_zoom_start"));
	auto& track_zoom_end_anim = element.animations.at(ui::hasher("track_zoom_end"));

	// Get current zoom window in timeline coordinates
	float zoom_start = track_zoom_start_anim.current;
	float zoom_end = track_zoom_end_anim.current;
	float zoom_range = zoom_end - zoom_start;

	// Ensure we have a valid range
	if (zoom_range <= 0.f) {
		zoom_range = active_video_duration;
		zoom_end = zoom_start + zoom_range;
	}

	// Convert to normalized coordinates (0-1) for compatibility with existing grab functions
	float visible_start = zoom_start / active_video_duration;
	float visible_range = zoom_range / active_video_duration;
	float visible_end = visible_start + visible_range;

	auto grab_rects = get_grab_rects(element, rect, visible_start, visible_range);

	struct GrabHandle {
		gfx::Rect rect;
		ui::AnimationState& anim;
		float* var_ptr = nullptr;
		float* min_ptr = nullptr;
		float* max_ptr = nullptr;
		void (*update_fn)(const ui::VideoElementData::Video&, float){};
		bool hovered = false;
		bool active = false;
	};

	std::array grabs = {
		GrabHandle{
			.rect = grab_rects.left.expand(GRAB_CLICK_EXPANSION),
			.anim = element.animations.at(ui::hasher("left_grab")),
			.var_ptr = video_data.start,
			.max_ptr = video_data.end,
			.update_fn =
				[](const auto& v, float p) {
					if (video_player)
						video_player->set_start(p);
				},
		},
		GrabHandle{
			.rect = grab_rects.right.expand(GRAB_CLICK_EXPANSION),
			.anim = element.animations.at(ui::hasher("right_grab")),
			.var_ptr = video_data.end,
			.min_ptr = video_data.start,
			.update_fn =
				[](const auto& v, float p) {
					if (video_player)
						video_player->set_end(p);
				},
		},
	};

	auto& handle_info = video_data.handle_info;
	bool grab_state = false;

	// Handle grab interactions
	for (auto [i, grab] : u::enumerate(grabs)) {
		std::string action = "grab_" + std::to_string(i);

		grab.hovered = grab.rect.contains(keys::mouse_pos) && set_hovered_element(element);

		if (grab.hovered) {
			ui::set_cursor(SDL_SYSTEM_CURSOR_POINTER);
			if (!ui::get_active_element() && keys::is_mouse_down())
				ui::set_active_element(element, action);
		}

		if (is_active_element(element, action)) {
			if (keys::is_mouse_down()) {
				grab.active = true;
				grab.anim.set_goal(1.f);

				if (!handle_info.grab_start_mouse_x) {
					handle_info.grab_start_mouse_x = keys::mouse_pos.x;
				}
				else if (handle_info.grab_moving || keys::mouse_pos.x != handle_info.grab_start_mouse_x) {
					handle_info.grab_moving = true;

					float local_mouse_percent =
						static_cast<float>(keys::mouse_pos.x - rect.x) / static_cast<float>(rect.w);
					local_mouse_percent = std::clamp(local_mouse_percent, 0.f, 1.0f);

					float timeline_percent = visible_start + (local_mouse_percent * visible_range);
					timeline_percent =
						video::frame_snap::snap_percent(timeline_percent, active_video_duration, active_video_fps);

					timeline_percent = std::clamp(
						timeline_percent, grab.min_ptr ? *grab.min_ptr : 0.f, grab.max_ptr ? *grab.max_ptr : 1.f
					);

					*grab.var_ptr = timeline_percent;
					grab.update_fn(*active_video, timeline_percent);

					auto& grab_progress_anim = element.animations.at(ui::hasher("progress"));
					grab_progress_anim.current = timeline_percent;
					grab_progress_anim.set_goal(timeline_percent);
				}

				float seek_percent = *grab.var_ptr;
				if (video_player)
					video_player->seek(seek_percent, true);
			}
			else {
				handle_info.grab_moving = false;
				handle_info.grab_start_mouse_x = {};

				ui::reset_active_element();
			}
		}

		if (!grab.active)
			grab.anim.set_goal(grab.hovered ? 0.5f : 0.f);

		updated |= grab.active;
		grab_state |= grab.active;
	}

	handle_info.grabbing = grab_state;

	auto& progress_anim = element.animations.at(ui::hasher("progress"));
	auto& seeking_anim = element.animations.at(ui::hasher("seeking"));
	auto& seek_anim = element.animations.at(ui::hasher("seek"));

	bool hovered = !updated && rect.contains(keys::mouse_pos) && set_hovered_element(element);
	bool active = ui::get_active_element() == &element;

	auto apply_pan = [&](float timeline_delta) {
		float new_start = track_zoom_start_anim.goal - timeline_delta;
		float new_end = track_zoom_end_anim.goal - timeline_delta;

		// Clamp to valid bounds
		if (new_start < 0.f) {
			float offset = -new_start;
			new_start = 0.f;
			new_end += offset;
		}
		if (new_end > active_video_duration) {
			float offset = new_end - active_video_duration;
			new_end = active_video_duration;
			new_start -= offset;
		}

		track_zoom_start_anim.set_goal(new_start);
		track_zoom_end_anim.set_goal(new_end);
		updated = true;
	};

	// scroll actions
	if (rect.contains(keys::mouse_pos)) {
		// panning (with horizontal scroll)
		if (keys::scroll_x_delta != 0.f) {
			float timeline_delta = (keys::scroll_x_delta / 30.f) * zoom_range;
			apply_pan(timeline_delta);

			keys::scroll_x_delta = 0.f;
		}

		// zooming
		if (keys::scroll_delta != 0.f) {
			float current_start = track_zoom_start_anim.goal;
			float current_end = track_zoom_end_anim.goal;
			float current_range = current_end - current_start;

			float zoom_factor = powf(TRACK_ZOOM_SPEED, keys::scroll_delta);

			float new_range = std::clamp(
				current_range * zoom_factor,
				TRACK_MAX_ZOOM_SECS,         // minimum visible range (most zoomed in)
				(float)active_video_duration // maximum range (fully zoomed out)
			);

			// get mouse position relative to rect (0..1)
			float mouse_local = (float(keys::mouse_pos.x - rect.x) / rect.w);
			mouse_local = std::clamp(mouse_local, 0.f, 1.f);

			// get mouse position in timeline coordinates
			float mouse_timeline = current_start + (mouse_local * current_range);

			// calculate new start/end
			float new_start = mouse_timeline - (mouse_local * new_range);
			float new_end = new_start + new_range;

			// clamp to valid timeline bounds
			if (new_start < 0.f) {
				new_start = 0.f;
				new_end = new_start + new_range;
			}
			if (new_end > active_video_duration) {
				new_end = active_video_duration;
				new_start = new_end - new_range;
			}

			track_zoom_start_anim.set_goal(new_start);
			track_zoom_end_anim.set_goal(new_end);

			updated = true;

			keys::scroll_delta = 0.f;
		}
	}

	// panning (with right click)
	std::string pan_action = "video_pan";

	if (hovered) {
		if (keys::is_mouse_down(SDL_BUTTON_RIGHT)) {
			// mark pan active
			if (!ui::get_active_element()) {
				set_active_element(element, pan_action);
			}
		}
		else if (keys::is_mouse_down()) {
			set_active_element(element, "video track");
		}
	}

	if (is_active_element(element, pan_action)) {
		if (keys::is_mouse_down(SDL_BUTTON_RIGHT)) {
			int cur_x = keys::mouse_pos.x;

			if (video_data.last_pan_x) {
				int last_x = *video_data.last_pan_x;
				int dx = cur_x - last_x;

				// convert pixel delta to timeline delta
				float timeline_delta = ((float)dx / rect.w) * zoom_range;
				apply_pan(timeline_delta);
			}

			video_data.last_pan_x = cur_x;
		}
		else {
			ui::reset_active_element();
			video_data.last_pan_x = {};
		}
	}

	// seeking
	if (is_active_element(element, "video track")) {
		if (keys::is_mouse_down()) {
			seeking_anim.set_goal(1.f);

			// local mouse percent on rect (0..1)
			float local_mouse_percent = (static_cast<float>(keys::mouse_pos.x - rect.x) / static_cast<float>(rect.w));
			local_mouse_percent = std::clamp(local_mouse_percent, 0.f, 1.f);

			// convert to timeline coordinates using zoom window
			float timeline_time = zoom_start + (local_mouse_percent * zoom_range);

			timeline_time = video::frame_snap::snap_time(timeline_time, active_video_fps);

			// convert to normalized percent for player
			float timeline_percent = timeline_time / active_video_duration;
			timeline_percent = std::clamp(timeline_percent, 0.f, 1.f);

			if (video_player)
				video_player->seek(timeline_percent, true);

			progress_anim.set_goal(timeline_percent);
			seek_anim.set_goal(timeline_percent);

			updated = true;
		}
		else {
			ui::reset_active_element();
		}
	}

	// hotkeys for start/end cut
	float current_percent = progress_anim.goal;

	// [/g = start
	if (keys::is_key_pressed(SDL_SCANCODE_LEFTBRACKET) || keys::is_key_pressed(SDL_SCANCODE_G)) {
		*video_data.start = std::clamp(current_percent, 0.f, video_data.end ? *video_data.end : 1.f);
		updated = true;
	}

	// ]/h = end
	if (keys::is_key_pressed(SDL_SCANCODE_RIGHTBRACKET) || keys::is_key_pressed(SDL_SCANCODE_H)) {
		*video_data.end = std::clamp(current_percent, video_data.start ? *video_data.start : 0.f, 1.f);
		updated = true;
	}

	// update anims
	if (video_player) {
		if (!video_player->get_queued_seek())
			seeking_anim.set_goal(0.f);
	}

	return updated;
}

bool update_videos_actual(const ui::Container& container, ui::AnimatedElement& element) {
	const auto& video_data = std::get<ui::VideoElementData>(element.element->data);

	const auto* active_video = get_active_video(element);

	auto track_rect = get_track_rect(element.element->rect.origin(), active_video->size);

	auto& offset_anim = element.animations.at(ui::hasher("video_offset"));
	auto rect = get_video_rect(element.element->rect.origin(), active_video->size, offset_anim.current);

	// clicking on videos
	std::vector<gfx::Rect> rects = get_video_rects(element, rect);

	for (auto [i, video] : u::enumerate(video_data.videos)) {
		if (!rects[i].contains(keys::mouse_pos))
			continue;

		if (i == *video_data.index) {
			if (video_player) {
				ui::set_cursor(SDL_SYSTEM_CURSOR_POINTER);

				if (keys::is_mouse_down()) {
					// same video, pause/unpause
					keys::on_mouse_press_handled(SDL_BUTTON_LEFT);
					video_player->cycle_paused();
				}
			}
		}
		else {
			ui::set_cursor(SDL_SYSTEM_CURSOR_POINTER);

			if (keys::is_mouse_down()) {
				// different video, switch to it
				keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

				if (video_player->get_current_file_path() != video.data.path)
					video_player->load_file(video.data.path);

				if (video_player)
					video_player->set_paused(true);

				*video_data.index = i;
			}
		}
	}

	return false;
}

bool ui::update_videos(const Container& container, AnimatedElement& element) {
	const auto& video_data = std::get<VideoElementData>(element.element->data);

	bool res = false;

	res |= update_videos_actual(container, element);
	res |= update_track(container, element);

	if (video_player) {
		video_player->set_start(*video_data.start);
		video_player->set_end(*video_data.end);
	}

	return res;
}

void ui::remove_videos(AnimatedElement& element) {
	const auto& video_data = std::get<VideoElementData>(element.element->data);

	for (const auto& video : video_data.videos) {
		gui_utils::delete_thumbnail(video.data.path);
	}

	video_player = nullptr;

	u::log("Removed all videos");
}

std::optional<ui::AnimatedElement*> ui::add_videos(
	const std::string& id,
	Container& container,
	const std::vector<UIVideo>& ui_videos,
	size_t& index,
	float& start,
	float& end,
	float& volume
) {
	if (ui_videos.empty())
		return {};

	if (!video_player) {
		video_player = std::make_shared<VideoPlayer>(volume);
		video_player->load_file(ui_videos[0].path);
	}

	std::vector<VideoElementData::Video> videos;

	for (auto [i, ui_video] : u::enumerate(ui_videos)) {
		auto size = fit_size(container, ui_video);

		VideoElementData::Video video{
			.data = ui_video,
			.size = size,
		};

		if (ui_video.video_info) {
			video.thumbnail = gui_utils::get_thumbnail(ui_video.path);

			auto waveform_res = get_waveform(ui_video.path, ui_video.video_info->duration);
			if (waveform_res) {
				video.waveform = *waveform_res;
			}
		}

		videos.emplace_back(std::move(video));
	}

	if (videos.size() == 0 || index < 0 || index >= videos.size())
		return {};

	auto& active_video = videos[index];

	auto track_rect = get_track_rect(container.current_position, active_video.size);

	Element element(
		id,
		ElementType::VIDEO,
		gfx::Rect{
			track_rect.x,
			container.current_position.y,
			track_rect.w,
			track_rect.y2() - container.current_position.y,
		},
		VideoElementData{
			.videos = std::move(videos),
			.index = &index,
			.start = &start,
			.end = &end,
		},
		render_videos,
		update_videos,
		remove_videos
	);

	auto offset = get_video_offset(element);

	auto* elem = add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(25.f) },
			{ hasher("video_offset"), AnimationState(25.f, offset ? *offset : 0.f, 1.f) },
			{ hasher("progress"), AnimationState(70.f) },
			{ hasher("seeking"), AnimationState(70.f) },
			{ hasher("seek"), AnimationState(70.f) },
			{ hasher("left_grab"), AnimationState(150.f) },
			{ hasher("right_grab"), AnimationState(150.f) },
		}
	);

	init_zoom(*elem);

	// some stuff has to update every time, not just on events
	update_progress(*elem);

	// update offset animation
	if (offset) {
		auto& offset_anim = elem->animations.at(ui::hasher("video_offset"));
		offset_anim.set_goal(*offset);
	};

	return elem;
}
