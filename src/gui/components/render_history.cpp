#include "render_history.h"

#include "notifications.h"

#include "../os/clipboard.h"
#include "../os/file_browser.h"
#include "../ui/keys.h"
#include "../render/render.h"
#include "../fonts/icons.h"

namespace history = gui::components::render_history;

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
		std::string title; // the file that came out, or the video that failed
		std::filesystem::path output_path;

		bool success;
		rendering::RenderError error;

		std::chrono::steady_clock::time_point shown_until; // fallback dismissal time if it is never hovered
		bool auto_display_hovered = false;                 // once hovered, leaving dismisses it immediately

		std::vector<uint8_t> thumbnail_jpeg;        // filled in by a worker thread
		std::shared_ptr<render::Texture> thumbnail; // uploaded from the jpeg on the render thread
		bool thumbnail_uploaded = false;
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
	std::string get_panel_title(bool hovered, size_t shown_count, size_t shown_failures) {
		if (hovered)
			return "Render history";

		bool plural = shown_count > 1;

		if (shown_failures == shown_count)
			return plural ? "Renders failed" : "Render failed";

		return plural ? "Renders finished" : "Render finished";
	}

	// pull a frame out of the video in the background, it shells out to ffmpeg
	void load_thumbnail_async(size_t entry_id, const std::filesystem::path& path, float timestamp) {
		std::thread([entry_id, path, timestamp] {
			std::vector<uint8_t> jpeg = u::get_video_frame_jpeg(path, timestamp);

			// short videos can have nothing at the offset, fall back to the very first frame
			if (jpeg.empty() && timestamp > 0.f)
				jpeg = u::get_video_frame_jpeg(path, 0.f);

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

	size_t add_entry(Entry entry) {
		std::lock_guard lock(entries_mutex);

		size_t id = next_entry_id++;
		entry.id = id;
		entry.shown_until = std::chrono::steady_clock::now() + ENTRY_SHOW_TIME;

		entries.insert(entries.begin(), std::make_shared<Entry>(std::move(entry)));

		if (entries.size() > MAX_ENTRIES)
			entries.resize(MAX_ENTRIES);

		return id;
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

	std::vector<ui::RenderHistoryAction> get_entry_actions(const std::shared_ptr<const Entry>& entry) {
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

void history::add_success(const rendering::RenderResult& result) {
	size_t id = add_entry(
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
	size_t id = add_entry(
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
	load_thumbnail_async(id, render.input_path, render.start + 0.2f);
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

void history::render_panel(ui::Container& container, float delta_time, bool with_button) {
	std::lock_guard lock(entries_mutex);

	if (!with_button)
		panel_open = false; // nothing to hover

	// Keep the origin around while the rows go stale so the backdrop can fold away with them.
	panel_collapse_rect =
		with_button && !button_rect.is_empty() ? std::optional<gfx::Rect>{ button_rect } : std::nullopt;
	if (!panel_collapse_rect)
		panel_transforming = false;

	auto now = std::chrono::steady_clock::now();

	bool hovering = panel_showing && panel_rect.contains(keys::mouse_pos);

	size_t shown_count = 0;
	size_t shown_failures = 0;

	for (const auto& entry_ptr : entries) {
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
		if (!entry.success)
			shown_failures++;

		// uploading a texture needs the render thread, so it happens here rather than in the worker
		if (!entry.thumbnail_uploaded && !entry.thumbnail_jpeg.empty()) {
			entry.thumbnail = render::texture_from_jpeg(entry.thumbnail_jpeg);
			entry.thumbnail_uploaded = true;
			entry.thumbnail_jpeg = {};
		}

		std::shared_ptr<const Entry> const_entry = entry_ptr;

		auto on_click = [const_entry] {
			if (const_entry->success)
				open_path(const_entry->output_path);
			else
				show_error_dialog(const_entry);
		};

		ui::add_render_history_entry(
			std::format("render history entry {}", entry.id),
			container,
			entry.title,
			entry.success ? "" : entry.error.user_message,
			!entry.success,
			entry.thumbnail,
			get_entry_actions(const_entry),
			std::move(on_click),
			// failed renders have no file to hand over, only the error to show
			entry.success ? std::make_optional(entry.output_path) : std::optional<std::filesystem::path>{},
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
	panel_title = get_panel_title(panel_open || hovering, shown_count, shown_failures);
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
