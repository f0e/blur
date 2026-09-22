#include "render_history.h"

#include "main.h"
#include "notifications.h"
#include "configs/configs.h"

#include "../renderer.h"
#include "../os/clipboard.h"
#include "../os/file_browser.h"
#include "../ui/keys.h"
#include "../render/render.h"
#include "../fonts/icons.h"
#include "common/media.h"

namespace history = gui::components::render_history;
using gui::components::main::MainScreen;

namespace {
	// how far outside the button/panel the mouse can stray before the panel closes. covers the gap between them
	const int HOVER_SLACK = 8;

	const float PANEL_ROUNDING = 8.f;
	const int PANEL_HEADER_GAP = 9;
	const gfx::Color PANEL_COLOR = { 14, 14, 14, 255 }; // opaque, so scrolling rows don't flash what's behind it
	const gfx::Color PANEL_BORDER_COLOR = gfx::Color::white(38);

	// how long a render sticks around on screen by itself before folding away into the button
	const auto ENTRY_SHOW_TIME = std::chrono::seconds(5);

	const size_t MAX_ENTRIES = 50;

	struct Entry {
		size_t id;
		std::string title; // the file that came out, or the video it came from
		std::filesystem::path output_path;

		bool success = false;
		rendering::RenderError error;

		// set while the render is still in the queue, so its row can show live progress and turn into the result
		std::shared_ptr<rendering::RenderState> state;
		bool active = false;
		size_t queue_index = 0;

		std::chrono::steady_clock::time_point shown_until; // fallback dismissal time if it is never hovered
		bool auto_display_hovered = false;                 // once hovered, leaving dismisses it immediately

		std::vector<uint8_t> thumbnail_jpeg;        // filled in by a worker thread
		std::shared_ptr<render::Texture> thumbnail; // uploaded from the jpeg on the render thread
	};

	struct ActiveStatus {
		std::string detail;
		std::optional<float> progress;
	};

	std::mutex entries_mutex;
	std::vector<std::shared_ptr<Entry>> entries; // newest first
	size_t next_entry_id = 0;

	bool panel_open = false;
	bool panel_showing = false; // whether it's got anything in it right now, ignoring what's animating away
	bool panel_transforming = false;
	gfx::Rect button_rect;
	gfx::Rect panel_rect;
	std::optional<gfx::Rect> panel_collapse_rect;
	float panel_height = 0.f;
	std::string panel_title;

	// the header shares its row with the button, which is drawn over the panel
	int header_height() {
		return std::max(fonts::dejavu.height(), history::BUTTON_SIZE) + PANEL_HEADER_GAP;
	}

	// the panel says what it is while the mouse is why it's showing, and what just happened when it shows itself
	// on its own
	std::string get_panel_title(bool hovered, size_t shown_count, size_t shown_failures, size_t shown_active) {
		if (hovered)
			return "Render history";

		if (shown_active > 0)
			return "Rendering";

		bool plural = shown_count > 1;

		if (shown_failures == shown_count)
			return plural ? "Renders failed" : "Render failed";

		return plural ? "Renders finished" : "Render finished";
	}

	// pull a frame out of the video in the background, it shells out to ffmpeg
	void load_thumbnail_async(size_t entry_id, const std::filesystem::path& path, float timestamp) {
		std::thread([entry_id, path, timestamp] {
			std::vector<uint8_t> jpeg = media::get_video_frame_jpeg(path, timestamp);

			// short videos can have nothing at the offset, fall back to the very first frame
			if (jpeg.empty() && timestamp > 0.f)
				jpeg = media::get_video_frame_jpeg(path, 0.f);

			if (jpeg.empty())
				return;

			std::lock_guard lock(entries_mutex);

			for (auto& entry : entries) {
				if (entry->id == entry_id) {
					entry->thumbnail_jpeg = std::move(jpeg);
					break;
				}
			}
		}).detach();
	}

	// a moment into the part of the video that's actually being rendered
	float get_input_thumbnail_timestamp(const rendering::VideoRenderDetails& render) {
		return static_cast<float>(render.video_info.video_start_time) + (render.start * render.video_info.duration) +
		       0.2f;
	}

	// the row that was showing the render's progress becomes the row for its result, so it stays put
	size_t finish_entry(const std::shared_ptr<rendering::RenderState>& state, Entry entry) {
		std::lock_guard lock(entries_mutex);

		entry.shown_until = std::chrono::steady_clock::now() + ENTRY_SHOW_TIME;

		auto it = std::ranges::find_if(entries, [&](const auto& existing) {
			return existing->state == state;
		});

		if (it != entries.end()) {
			auto& existing = **it;

			entry.id = existing.id;
			entry.state = existing.state; // dropped once the render leaves the queue, see sync_active_entries
			entry.thumbnail = existing.thumbnail;

			existing = std::move(entry);

			return existing.id;
		}

		size_t id = next_entry_id++;
		entry.id = id;

		entries.insert(entries.begin(), std::make_shared<Entry>(std::move(entry)));

		if (entries.size() > MAX_ENTRIES)
			entries.resize(MAX_ENTRIES);

		return id;
	}

	// the queue owns the rows for renders that haven't finished yet
	void sync_active_entries() {
		auto queue = rendering::video_render_queue.get_queue_copy();

		std::vector<const rendering::RenderState*> queued;
		queued.reserve(queue.size());

		for (const auto& [i, render] : u::enumerate(queue)) {
			queued.push_back(render.state.get());

			auto it = std::ranges::find_if(entries, [&](const auto& entry) {
				return entry->state == render.state;
			});

			if (it != entries.end()) {
				(*it)->queue_index = i;
				continue;
			}

			bool is_current_render = i == 0;
			bool auto_show = !is_current_render || gui::components::main::current_screen() != MainScreen::PROGRESS;

			auto entry = std::make_shared<Entry>(Entry{
				.id = next_entry_id++,
				.title = u::path_to_string(render.input_path.stem()),
				.state = render.state,
				.active = true,
				.queue_index = i,
				.shown_until = auto_show ? std::chrono::steady_clock::now() + ENTRY_SHOW_TIME
			                             : std::chrono::steady_clock::time_point{},
			});

			entries.insert(entries.begin(), entry);

			load_thumbnail_async(entry->id, render.input_path, get_input_thumbnail_timestamp(render));
		}

		auto is_queued = [&](const Entry& entry) {
			return u::contains(queued, entry.state.get());
		};

		// a render that left the queue without a result was cancelled, so its row goes with it
		std::erase_if(entries, [&](const auto& entry) {
			return entry->active && !is_queued(*entry);
		});

		// nothing left to keep the render state alive for once it's out of the queue
		for (auto& entry : entries) {
			if (entry->state && !is_queued(*entry))
				entry->state.reset();
		}
	}

	std::vector<std::shared_ptr<Entry>> get_ordered_entries() {
		std::vector<std::shared_ptr<Entry>> ordered;
		ordered.reserve(entries.size());

		for (const auto& entry : entries) {
			if (entry->active)
				ordered.push_back(entry);
		}

		std::ranges::sort(ordered, std::ranges::greater{}, [](const auto& entry) {
			return entry->queue_index;
		});

		for (const auto& entry : entries) {
			if (!entry->active)
				ordered.push_back(entry);
		}

		return ordered;
	}

	ActiveStatus get_active_status(const Entry& entry) {
		if (entry.queue_index > 0)
			return { .detail = "Queued" };

		auto progress = entry.state->get_progress();

		if (!progress.rendered_a_frame) {
			if (entry.state->is_paused())
				return { .detail = "Paused" };

			switch (progress.init_stage) {
				case rendering::RenderState::InitStage::generating_mask:
					return { .detail = "Generating mask..." };
				case rendering::RenderState::InitStage::building_engine:
					return { .detail = "Building TensorRT engine..." };
				case rendering::RenderState::InitStage::none:
					return { .detail = "Initialising..." };
			}
		}

		float fraction = progress.total_frames > 0
		                     ? std::clamp(progress.current_frame / static_cast<float>(progress.total_frames), 0.f, 1.f)
		                     : 0.f;

		std::string detail = std::format("{:.0f}%", fraction * 100.f);

		if (entry.state->is_paused())
			detail = std::format("Paused - {}", detail);
		else if (progress.fps > 0.f)
			detail = std::format("{} - {:.0f} frames per second", detail, progress.fps);

		return { .detail = detail, .progress = fraction };
	}

	void open_path(const std::filesystem::path& path) {
		// file urls open the file in its default player, or the folder in the system file browser
		std::string url = std::format("file://{}", u::path_to_string(path));

		if (!SDL_OpenURL(url.c_str()))
			u::log_error("Failed to open '{}': {}", u::path_to_string(path), SDL_GetError());
	}

	void show_error_dialog(const std::shared_ptr<const Entry>& entry) {
		ui::dialog::open(
			{
				.title = "Render failed",
				.content =
					[entry](ui::Container& container) {
						const auto log_font = fonts::dejavu(fonts::size::SMALL);

						// element state is keyed by id and outlives the dialog, so key the ids by entry
						auto element_id = [id = entry->id](std::string_view name) {
							return std::format("error {} {}", id, name);
						};

						ui::dialog::add_body(
							container, element_id("body"), std::format("{} could not be rendered.", entry->title)
						);
						ui::dialog::add_field(container, element_id("message"), "Error", entry->error.user_message);

						// errors that came through as a bare message have none of these
						std::vector<std::pair<std::string, const std::string*>> logs{
							{ "Technical details", &entry->error.technical_details },
							{ "VSPipe log", &entry->error.vspipe_errors },
							{ "FFmpeg log", &entry->error.ffmpeg_errors },
						};

						bool heading_added = false;
						for (const auto& [title, text] : logs) {
							if (text->empty())
								continue;

							if (!heading_added) {
								ui::dialog::add_heading(container, element_id("advanced"), "Advanced diagnostics");
								heading_added = true;
							}

							ui::dialog::add_field(container, element_id(title), title, *text, log_font);
						}
					},
				.close_on_confirm = false,
				.width = 560,
				.confirm_text = "Copy error",
				.cancel_text = "Close",
				.confirm_icon = icons::COPY,
				.on_confirm =
					[entry] {
						SDL_SetClipboardText(entry->error.to_string().c_str());

						gui::components::notifications::add(
							"Copied error message to clipboard",
							ui::NotificationType::INFO,
							{},
							std::chrono::duration<float>(2.f)
						);
					},
			}
		);
	}

	void go_to_render_screen() {
		gui::components::main::show_screen(MainScreen::PROGRESS);

		if (gui::renderer::screen != gui::renderer::Screens::CONFIG)
			return;

		gui::components::configs::leave_screen([] {
			gui::renderer::screen = gui::renderer::Screens::MAIN;
		});
	}

	std::vector<ui::RenderHistoryAction> get_active_entry_actions(const std::shared_ptr<const Entry>& entry) {
		std::vector<ui::RenderHistoryAction> actions;

		// only the render at the front is going, the rest haven't started so there's nothing to pause
		if (entry->queue_index == 0) {
			actions.push_back(
				{
					.label = entry->state->is_paused() ? "Resume" : "Pause",
					.on_press =
						[state = entry->state] {
							state->toggle_pause();
						},
				}
			);
		}

		actions.push_back(
			{
				.label = "Cancel",
				.on_press =
					[state = entry->state] {
						rendering::video_render_queue.cancel(state);
					},
			}
		);

		return actions;
	}

	std::vector<ui::RenderHistoryAction> get_entry_actions(const std::shared_ptr<const Entry>& entry) {
		if (entry->active)
			return get_active_entry_actions(entry);

		if (!entry->success) {
			return {
				{
					.label = "View details",
					.on_press =
						[entry] {
							show_error_dialog(entry);
						},
				},
			};
		}

		return {
			{
				.icon = icons::COPY,
				.tooltip = "Copy video",
				.on_press =
					[entry] {
						const auto& path = entry->output_path;

						if (!os::clipboard::copy_file(path)) {
							u::log_error("Failed to copy '{}' to the clipboard", u::path_to_string(path));
							return;
						}

						gui::components::notifications::add(
							"Copied video to clipboard",
							ui::NotificationType::INFO,
							{},
							std::chrono::duration<float>(2.f)
						);
					},
			},
			{
				.icon = icons::FOLDER,
				.tooltip = "Open containing folder",
				.on_press =
					[entry] {
						const auto& path = entry->output_path;

						if (!os::file_browser::reveal_file(path))
							u::log_error("Failed to reveal '{}' in the file browser", u::path_to_string(path));
					},
			},
		};
	}
}

void history::add_success(const rendering::VideoRenderDetails& render, const rendering::RenderResult& result) {
	size_t id = finish_entry(
		render.state,
		{
			.title = u::path_to_string(result.output_path.filename()),
			.output_path = result.output_path,
			.success = true,
		}
	);

	load_thumbnail_async(id, result.output_path, 0.2f);
}

void history::add_failure(
	const rendering::VideoRenderDetails& render, const std::variant<std::string, rendering::RenderError>& error
) {
	size_t id = finish_entry(
		render.state,
		{
			.title = u::path_to_string(render.input_path.stem()),
			.success = false,
			// a bare string is an error with nothing else to show
			.error = std::holds_alternative<rendering::RenderError>(error)
	                     ? std::get<rendering::RenderError>(error)
	                     : rendering::RenderError{ .user_message = std::get<std::string>(error) },
		}
	);

	// no output to show, so use the video that failed
	load_thumbnail_async(id, render.input_path, get_input_thumbnail_timestamp(render));
}

bool history::empty() {
	std::lock_guard lock(entries_mutex);
	return entries.empty();
}

void history::render_button(ui::Container& container) {
	if (empty()) {
		panel_open = false;
		return;
	}

	// only the button opens the history. hovering the panel keeps an already open one open, so you can reach into it
	panel_open = button_rect.expand(HOVER_SLACK).contains(keys::mouse_pos) ||
	             (panel_open && panel_showing && panel_rect.expand(HOVER_SLACK).contains(keys::mouse_pos));

	auto* button = ui::add_icon_button(
		"render history button",
		container,
		icons::HISTORY,
		fonts::icons,
		gfx::Size(BUTTON_SIZE, BUTTON_SIZE),
		gfx::Color::white(panel_open ? 255 : 120),
		gfx::Color::white(),
		{},
		panel_open ? "" : "Render history"
	);

	button_rect = button->element->rect;
}

void history::render_panel(ui::Container& container, float delta_time) {
	std::lock_guard lock(entries_mutex);

	sync_active_entries();

	// Keep the origin around while the rows go stale so the backdrop can fold away with them.
	panel_collapse_rect = !button_rect.is_empty() ? std::optional<gfx::Rect>{ button_rect } : std::nullopt;
	if (!panel_collapse_rect)
		panel_transforming = false;

	auto now = std::chrono::steady_clock::now();

	bool hovering = panel_showing && panel_rect.contains(keys::mouse_pos);

	size_t shown_count = 0;
	size_t shown_failures = 0;
	size_t shown_active = 0;

	for (const auto& entry_ptr : get_ordered_entries()) {
		auto& entry = *entry_ptr;

		// The timer is only a fallback for entries the user never interacts with. Once an auto-shown entry has
		// been hovered, keep it under the cursor and dismiss it as soon as the cursor leaves.
		if (!panel_open && (entry.auto_display_hovered ? !hovering : now > entry.shown_until))
			continue;

		// hovering the panel, or opening the full history, counts as interacting with an auto-shown entry
		if (hovering)
			entry.auto_display_hovered = true;

		if (shown_count == 0)
			container.current_position.y += header_height(); // the header is drawn with the backdrop, behind the rows

		shown_count++;
		if (entry.active)
			shown_active++;
		else if (!entry.success)
			shown_failures++;

		// uploading a texture needs the render thread, so it happens here rather than in the worker
		if (!entry.thumbnail_jpeg.empty()) {
			entry.thumbnail = render::texture_from_jpeg(entry.thumbnail_jpeg);
			entry.thumbnail_jpeg = {};
		}

		std::shared_ptr<const Entry> const_entry = entry_ptr;

		ActiveStatus status;
		std::string detail;

		if (entry.active) {
			status = get_active_status(entry);
			detail = status.detail;
		}
		else if (!entry.success) {
			detail = entry.error.user_message;
		}

		std::optional<std::function<void()>> on_click;
		if (entry.active) {
			on_click = go_to_render_screen;
		}
		else {
			on_click = [const_entry] {
				if (const_entry->success)
					open_path(const_entry->output_path);
				else
					show_error_dialog(const_entry);
			};
		}

		ui::add_render_history_entry(
			std::format("render history entry {}", entry.id),
			container,
			entry.title,
			detail,
			!entry.active && !entry.success,
			status.progress,
			entry.thumbnail,
			get_entry_actions(const_entry),
			std::move(on_click),
			// only a finished render has a file to hand over
			entry.active || !entry.success ? std::optional<std::filesystem::path>{}
										   : std::make_optional(entry.output_path),
			panel_collapse_rect,
			fonts::dejavu
		);
	}

	if (shown_count == 0) {
		if (panel_showing && panel_collapse_rect)
			panel_transforming = true;

		// keep the last rect around so the backdrop can animate out in place rather than snapping shut under the rows
		panel_showing = false;
		return;
	}

	int padding_bottom = container.padding ? container.padding->bottom : 0;
	int content_height = container.current_position.y - container.element_gap + padding_bottom - container.rect.y;

	float goal_height = static_cast<float>(std::min(content_height, container.rect.h));

	if (!panel_showing && panel_collapse_rect)
		panel_transforming = true;

	// grows and shrinks with the rows, but starts at the right size rather than unfolding from whatever it was
	panel_height = panel_showing ? u::lerp(panel_height, goal_height, 25.f * delta_time, 0.5f) : goal_height;
	panel_showing = true;

	panel_rect = { container.rect.x, container.rect.y, container.rect.w, static_cast<int>(std::lround(panel_height)) };
	panel_title = get_panel_title(panel_open || hovering, shown_count, shown_failures, shown_active);
}

void history::draw_panel(ui::Container& container, ui::Container& button_container) {
	// the button is always visible, whether or not the panel has anything to show
	if (panel_rect.is_empty() || container.elements.empty()) {
		ui::render_container(button_container);
		return;
	}

	// Follows the rows' shared animation: from the button to its full bounds on the way in, and back on the way out.
	float anim = 0.f;
	for (const auto& [id, element] : container.elements) {
		anim = std::max(anim, element.animations.at(ui::hasher("main")).current);
	}

	if (anim <= 0.01f) {
		if (!panel_showing)
			panel_transforming = false;

		ui::render_container(button_container);
		return;
	}

	// the fade-out end is handled by the early return above, so settling at the top is the only case left
	if (panel_transforming && panel_showing && anim >= 1.f)
		panel_transforming = false;

	bool transform_contents = panel_transforming && panel_collapse_rect.has_value();
	gfx::Rect animated_panel_rect =
		transform_contents ? gfx::Rect::lerp(*panel_collapse_rect, panel_rect, anim) : panel_rect;
	float draw_opacity = transform_contents ? 1.f : anim;
	size_t first_vertex = render::draw_vertex_count();

	render::rounded_rect_filled(panel_rect, PANEL_COLOR.adjust_alpha(draw_opacity), PANEL_ROUNDING);
	render::rounded_rect_stroke(panel_rect, PANEL_BORDER_COLOR.adjust_alpha(draw_opacity), PANEL_ROUNDING);

	// the header sits with the backdrop rather than in the container, so scrolling doesn't drag it away.
	// it shares its row with the button, so it's centered against that. it stays outside the row overflow clip so
	// its transformed glyphs aren't cut off while the panel is expanding
	gfx::Rect usable = container.get_usable_rect();
	int header_top = panel_rect.y + (container.padding ? container.padding->top : 0);

	render::text(
		gfx::Point(usable.x, header_top + ((BUTTON_SIZE - fonts::dejavu.height()) / 2)),
		gfx::Color::white(110).adjust_alpha(draw_opacity),
		panel_title,
		fonts::dejavu
	);

	// transform the backdrop/header now, before the button is drawn, so the button itself is excluded and stays
	// put rather than folding away with them
	if (transform_contents)
		render::transform_draw_vertices(first_vertex, panel_rect, animated_panel_rect, anim);

	// drawn above the backdrop but below the rows: scrolled-over entries cover it back up
	ui::render_container(button_container);

	size_t rows_first_vertex = render::draw_vertex_count();

	// The rows are submitted at the panel's settled coordinates and only moved into animated_panel_rect afterwards,
	// so this has to clip against the settled rect. Clipping against the animated one would cull the glyphs before
	// the transform ever ran (imgui drops text outside the clip rect at submission time, see ImFont::RenderText).
	// animated_panel_rect is always inside panel_rect, so scroll overflow is still clipped either way.
	render::push_clip_rect(panel_rect, true);

	std::vector<std::pair<ui::AnimationState*, float>> row_animations;
	if (transform_contents) {
		row_animations.reserve(container.elements.size());
		for (auto& [id, element] : container.elements) {
			auto& row_animation = element.animations.at(ui::hasher("main"));
			row_animations.emplace_back(&row_animation, row_animation.current);
			row_animation.current = 1.f;
		}
	}

	// the rows live inside the panel, nothing of them shows past its edges
	ui::render_container(container);

	for (const auto& [row_animation, current] : row_animations)
		row_animation->current = current;

	render::pop_clip_rect();

	if (transform_contents)
		render::transform_draw_vertices(rows_first_vertex, panel_rect, animated_panel_rect, anim);
}
