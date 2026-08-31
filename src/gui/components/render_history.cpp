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
		std::string name;
		std::filesystem::path output_path;

		bool success;
		std::string error_message;   // the short version, shown in the entry
		std::string error_clipboard; // the full log, copied by the entry's copy button

		std::chrono::steady_clock::time_point shown_until; // fallback dismissal time if it is never hovered
		bool auto_display_hovered = false;                 // once hovered, leaving dismisses it immediately

		std::vector<uint8_t> thumbnail_jpeg;        // filled in by a worker thread
		std::shared_ptr<render::Texture> thumbnail; // uploaded from the jpeg on the render thread
		bool thumbnail_uploaded = false;
	};

	std::mutex entries_mutex;
	std::vector<Entry> entries; // newest first
	size_t next_entry_id = 0;

	bool panel_open = false;
	bool panel_showing = false; // whether it's got anything in it right now, ignoring what's animating away
	gfx::Rect button_rect;
	gfx::Rect panel_rect;
	float panel_height = 0.f;
	std::string panel_title;

	// the header shares its row with the button, which is drawn over the panel
	int header_height() {
		return std::max(fonts::dejavu.height(), history::BUTTON_SIZE) + PANEL_HEADER_GAP;
	}

	// the panel says what it is when you open it, and what just happened when it shows itself
	std::string get_panel_title(bool open, size_t shown_count, size_t shown_failures) {
		if (open)
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
				if (entry.id == entry_id) {
					entry.thumbnail_jpeg = std::move(jpeg);
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

		entries.insert(entries.begin(), std::move(entry));

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

	std::vector<ui::RenderHistoryAction> get_entry_actions(const Entry& entry) {
		if (!entry.success) {
			return {
				{
					.icon = icons::COPY,
					.tooltip = "Copy error",
					.on_press =
						[error = entry.error_clipboard] {
							SDL_SetClipboardText(error.c_str());

							gui::components::notifications::add(
								"Copied error message to clipboard",
								ui::NotificationType::INFO,
								{},
								std::chrono::duration<float>(2.f)
							);
						},
				},
			};
		}

		return {
			{
				.icon = icons::COPY,
				.tooltip = "Copy video",
				.on_press =
					[path = entry.output_path] {
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
					[path = entry.output_path] {
						if (!os::file_browser::reveal_file(path))
							u::log_error("Failed to reveal '{}' in the file browser", u::path_to_string(path));
					},
			},
		};
	}
}

void history::add_success(const rendering::VideoRenderDetails& render, const rendering::RenderResult& result) {
	size_t id = add_entry(
		{
			.name = u::path_to_string(render.input_path.stem()),
			.output_path = result.output_path,
			.success = true,
		}
	);

	load_thumbnail_async(id, result.output_path, 0.2f);
}

void history::add_failure(
	const rendering::VideoRenderDetails& render, const std::variant<std::string, rendering::RenderError>& error
) {
	std::string message;
	std::string clipboard;

	if (std::holds_alternative<rendering::RenderError>(error)) {
		const auto& render_error = std::get<rendering::RenderError>(error);
		message = render_error.user_message;
		clipboard = render_error.to_string();
	}
	else {
		message = std::get<std::string>(error);
		clipboard = message;
	}

	size_t id = add_entry(
		{
			.name = u::path_to_string(render.input_path.stem()),
			.success = false,
			.error_message = message,
			.error_clipboard = clipboard,
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

	auto now = std::chrono::steady_clock::now();

	bool hovering = panel_showing && panel_rect.contains(keys::mouse_pos);

	size_t shown_count = 0;
	size_t shown_failures = 0;

	for (auto& entry : entries) {
		if (!panel_open) {
			// The timer is only a fallback for entries the user never interacts with. Once an auto-shown
			// entry has been hovered, keep it under the cursor and dismiss it as soon as the cursor leaves.
			if (entry.auto_display_hovered) {
				if (!hovering)
					continue;
			}
			else {
				if (now > entry.shown_until)
					continue;

				if (hovering)
					entry.auto_display_hovered = true;
			}
		}
		else if (hovering) {
			// Opening the full history while an entry is still auto-shown also counts as interacting with it.
			entry.auto_display_hovered = true;
		}

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

		std::optional<std::function<void()>> on_click;
		if (entry.success) {
			on_click = [path = entry.output_path] {
				open_path(path);
			};
		}

		ui::add_render_history_entry(
			std::format("render history entry {}", entry.id),
			container,
			entry.name,
			entry.success ? u::path_to_string(entry.output_path.filename()) : entry.error_message,
			!entry.success,
			entry.thumbnail,
			get_entry_actions(entry),
			std::move(on_click),
			with_button && !button_rect.is_empty() ? std::optional<gfx::Rect>{ button_rect } : std::nullopt,
			fonts::dejavu
		);
	}

	if (shown_count == 0) {
		// keep the last rect around so the backdrop fades out in place rather than snapping shut under the rows
		panel_showing = false;
		return;
	}

	int padding_bottom = container.padding ? container.padding->bottom : 0;
	int content_height = container.current_position.y - container.element_gap + padding_bottom - container.rect.y;

	float goal_height = static_cast<float>(std::min(content_height, container.rect.h));

	// grows and shrinks with the rows, but starts at the right size rather than unfolding from whatever it was
	panel_height = panel_showing ? u::lerp(panel_height, goal_height, 25.f * delta_time, 0.5f) : goal_height;
	panel_showing = true;

	panel_rect = { container.rect.x, container.rect.y, container.rect.w, static_cast<int>(std::lround(panel_height)) };
	panel_title = get_panel_title(panel_open, shown_count, shown_failures);
}

void history::draw_panel(ui::Container& container) {
	if (panel_rect.is_empty() || container.elements.empty())
		return;

	// fades with the rows it's holding
	float anim = 0.f;
	for (const auto& [id, element] : container.elements) {
		anim = std::max(anim, element.animations.at(ui::hasher("main")).current);
	}

	if (anim <= 0.01f)
		return;

	render::rounded_rect_filled(panel_rect, PANEL_COLOR.adjust_alpha(anim), PANEL_ROUNDING);
	render::rounded_rect_stroke(panel_rect, PANEL_BORDER_COLOR.adjust_alpha(anim), PANEL_ROUNDING);

	// the header sits with the backdrop rather than in the container, so scrolling doesn't drag it away.
	// it shares its row with the button, so it's centered against that
	gfx::Rect usable = container.get_usable_rect();
	int header_top = panel_rect.y + (container.padding ? container.padding->top : 0);

	render::text(
		gfx::Point(usable.x, header_top + ((BUTTON_SIZE - fonts::dejavu.height()) / 2)),
		gfx::Color::white(110).adjust_alpha(anim),
		panel_title,
		fonts::dejavu
	);

	// the rows live inside the panel, nothing of them shows past its edges
	render::push_clip_rect(panel_rect, true);
	ui::render_container(container);
	render::pop_clip_rect();
}
