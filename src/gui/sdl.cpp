#include "sdl.h"
#include "common/config_app.h"
#include "render/render.h"
#include "os/desktop_notification.h"
#include "os/taskbar.h"
#include "os/window.h"
#include "ui/ui.h"
#include "renderer.h"
#include "gui.h"

namespace {
	std::unordered_map<SDL_SystemCursor, SDL_Cursor*> cursor_cache;
	SDL_SystemCursor current_cursor = SDL_SYSTEM_CURSOR_DEFAULT;
	bool set_cursor_this_frame = false;

	// app config file watching (for live-reloading settings like dpi scale)
	const Uint64 CONFIG_POLL_INTERVAL_MS = 1000;
	std::filesystem::file_time_type last_config_write;
	bool has_config_write_time = false;
	Uint64 last_config_check_ms = 0;

	SDL_EGLAttrib* SDLCALL angle_platform_attributes(void*) {
		auto* attributes = static_cast<SDL_EGLAttrib*>(SDL_malloc(3 * sizeof(SDL_EGLAttrib)));
		if (!attributes)
			return nullptr;

		attributes[0] = EGL_PLATFORM_ANGLE_TYPE_ANGLE;
#ifdef _WIN32
		attributes[1] = EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE;
#elif defined(__APPLE__)
		attributes[1] = EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE;
#else
		attributes[1] = EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE;
#endif
		attributes[2] = EGL_NONE;
		return attributes;
	}

	std::filesystem::path angle_library_dir() {
		auto exe_dir = std::filesystem::path(u::get_executable_path()).parent_path();

#ifdef __APPLE__
		auto frameworks_dir = exe_dir.parent_path() / "Frameworks";

		std::error_code ec;
		if (std::filesystem::is_directory(frameworks_dir, ec))
			return frameworks_dir;
#endif

		return exe_dir;
	}

	void remember_config_write_time() {
		std::error_code ec;
		auto write_time = std::filesystem::last_write_time(config_app::get_app_config_path(), ec);
		if (!ec) {
			last_config_write = write_time;
			has_config_write_time = true;
		}
	}

	void resize_window_to_scale(SDL_Window* window, float logical_w, float logical_h, float content_scale) {
		int new_w =
			std::max(int(std::lround(logical_w * content_scale)), int(sdl::MINIMUM_WINDOW_SIZE.w * content_scale));
		int new_h =
			std::max(int(std::lround(logical_h * content_scale)), int(sdl::MINIMUM_WINDOW_SIZE.h * content_scale));

		SDL_SetWindowSize(window, new_w, new_h);
	}

	// returns true if something that's already on screen needs redrawing
	bool apply_app_config(const GlobalAppSettings& config) {
		bool highlight_color_changed = false;

#ifdef BLUR_COLOR_THEMES
		auto parsed_highlight_color = gfx::Color::from_hex_string(config.gui_color_hex, false);
		auto highlight_color = parsed_highlight_color.value_or(ui::DEFAULT_HIGHLIGHT_COLOR);

		highlight_color_changed = highlight_color != ui::highlight_color;
		ui::highlight_color = highlight_color;
#endif

		float previous_scale = render::dpi_scale_override;
		float previous_content_scale = sdl::window ? render::get_content_scale(sdl::window) : 1.f;

		render::dpi_scale_override = config.dpi_scale_override;

		bool scale_changed = previous_scale != render::dpi_scale_override;

		if (sdl::window) {
			// live-reloadable: turning it off mid-session should clear whatever's on the icon
			if (config.taskbar_progress)
				os::taskbar::initialise(sdl::window);
			else
				os::taskbar::cleanup();

			// MINIMUM_WINDOW_SIZE is in scaled (logical) design units, but sdl wants window coordinates,
			// so scale it up by the os content scale to keep the same usable minimum on high-dpi displays
			float content_scale = render::get_content_scale(sdl::window);
			SDL_SetWindowMinimumSize(
				sdl::window,
				int(sdl::MINIMUM_WINDOW_SIZE.w * content_scale),
				int(sdl::MINIMUM_WINDOW_SIZE.h * content_scale)
			);

			if (scale_changed && previous_content_scale > 0.f && content_scale != previous_content_scale) {
				int current_w = 0;
				int current_h = 0;
				SDL_GetWindowSize(sdl::window, &current_w, &current_h);

				// divide out the old scale to get back to logical units before applying the new one
				resize_window_to_scale(
					sdl::window, current_w / previous_content_scale, current_h / previous_content_scale, content_scale
				);
			}
		}

		return scale_changed || highlight_color_changed;
	}
}

tl::expected<void, std::string> sdl::initialise() {
	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1"); // idk mpv example says to
	// SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "1");
	SDL_SetHint(
		SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1"
	); // allows the screen to auto sleep. WHY IS THIS DISABLED BY DEFAULT?

	// Use ANGLE's EGL/GLES implementation on every platform. The backing renderer
	// is selected below (D3D11 on Windows, Metal on macOS, OpenGL on Linux).
	SDL_SetHint(SDL_HINT_VIDEO_FORCE_EGL, "1");
	SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");

	// Use the packaged libraries instead of another ANGLE installation that
	// happens to be on the system loader's search path.
	const auto angle_dir = angle_library_dir();
	const std::string angle_egl_path = (angle_dir / BLUR_ANGLE_EGL_LIBRARY).string();
	const std::string angle_gles_path = (angle_dir / BLUR_ANGLE_GLES_LIBRARY).string();

	for (const auto& path : { angle_egl_path, angle_gles_path }) {
		if (!std::filesystem::exists(path))
			u::log_error("ANGLE library missing: {} (the build didn't package it next to the app)", path);
	}

	SDL_SetHint(SDL_HINT_EGL_LIBRARY, angle_egl_path.c_str());
	SDL_SetHint(SDL_HINT_OPENGL_LIBRARY, angle_gles_path.c_str());

	if (!SDL_Init(SDL_INIT_VIDEO))
		return tl::unexpected(std::format("SDL initialization failed: {}", SDL_GetError()));

	if (!SDL_GL_SetAttribute(SDL_GL_EGL_PLATFORM, EGL_PLATFORM_ANGLE_ANGLE))
		return tl::unexpected(std::format("Failed to select ANGLE's EGL platform: {}", SDL_GetError()));

	SDL_EGL_SetAttributeCallbacks(angle_platform_attributes, nullptr, nullptr, nullptr);

	auto config = config_app::get_app_config();

	// Initialise notification system
	if (config.render_success_notifications || config.render_failure_notifications) {
		desktop_notification::initialise(APPLICATION_NAME);
	}

	// GL ES 3.0 + GLSL 300 es (WebGL 2.0)
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

	// Create window with graphics context
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	// create sdl window
	window = SDL_CreateWindow(
		"Blur",
		config.gui_width,
		config.gui_height,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
	);

	if (!window)
		return tl::unexpected(std::format("Failed to create SDL window: {}", SDL_GetError()));

	apply_app_config(config);
	remember_config_write_time();

	// scale the default window size if needed
	float initial_content_scale = render::get_content_scale(window);
	if (initial_content_scale != 1.f) {
		resize_window_to_scale(window, (float)config.gui_width, (float)config.gui_height, initial_content_scale);
	}

	SDL_AddEventWatch(event_watcher, window);

	// enable drag and drop
	SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);

	// create OpenGL ES context
	gl_context = SDL_GL_CreateContext(window);
	if (!gl_context)
		return tl::unexpected(std::format("Failed to create SDL GL context: {}", SDL_GetError()));

	if (!SDL_GL_MakeCurrent(window, gl_context))
		return tl::unexpected(std::format("Failed to activate SDL GL context: {}", SDL_GetError()));

	const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
	const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
	u::log("graphics backend: {} ({})", renderer ? renderer : "unknown", version ? version : "unknown");

	SDL_GL_SetSwapInterval(1); // enable vsync
	SDL_ShowWindow(window);

	if (!os::window::disable_live_resize_scaling(window)) {
		// not fatal, resizing just looks worse
		u::log_error("failed to stop the compositor scaling the window while it's being resized");
	}

	if (!render::init(window, gl_context))
		return tl::unexpected("Failed to initialise rendering");

	return {};
}

void sdl::cleanup() {
	for (auto& [_, cursor] : cursor_cache) {
		SDL_DestroyCursor(cursor);
	}
	cursor_cache.clear();

	render::destroy();

	if (sdl::gl_context) {
		SDL_GL_DestroyContext(sdl::gl_context);
		sdl::gl_context = nullptr;
	}

	// takes the progress off the icon, so it has to happen while the window's still around
	os::taskbar::cleanup();

	if (sdl::window) {
		SDL_RemoveEventWatch(event_watcher, window);

		SDL_DestroyWindow(sdl::window);
		sdl::window = nullptr;
	}

	desktop_notification::cleanup();

	SDL_Quit();
}

bool sdl::event_watcher(void* data, SDL_Event* event) {
	switch (event->type) {
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		case SDL_EVENT_WINDOW_EXPOSED: {
			SDL_Window* win = SDL_GetWindowFromID(event->window.windowID);
			if (win == static_cast<SDL_Window*>(data)) {
				// while the window's being resized (or moved) the os runs its own modal loop, so our main loop is
				// stuck inside SDL_PollEvent and this watcher is the only chance we get to draw. draw a whole frame
				// rather than an empty one, otherwise the window is black until the user lets go
				static bool redrawing = false; // just in case rendering ends up pumping events

				if (render::initialised && !redrawing) {
					redrawing = true;
					gui::renderer::redraw_window(true, true); // note: also keeps dpi scaling in sync while resizing
					redrawing = false;
				}

				// and draw again once the main loop's unblocked, in case anything changed while we were stuck
				gui::to_render = true;
			}
			break;
		}

		default:
			break;
	}

	return true;
}

void sdl::on_frame_start() {
	set_cursor_this_frame = false;
}

void sdl::set_cursor(SDL_SystemCursor cursor) {
	if (current_cursor != cursor) {
		current_cursor = cursor;

		// Check if cursor is already cached
		if (!cursor_cache.contains(cursor)) {
			cursor_cache[cursor] = SDL_CreateSystemCursor(cursor);
		}

		SDL_SetCursor(cursor_cache[cursor]);
	}

	set_cursor_this_frame = true;
}

void sdl::update_vsync() {
	static int last_display_index = -1;

	int display_index = SDL_GetDisplayForWindow(window);
	if (display_index != last_display_index) {
		last_display_index = display_index;

		const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display_index);
		if (mode) {
			double rate = mode->refresh_rate;
			vsync_frame_time_ms = float(1.f / (rate + VSYNC_EXTRA_FPS)) * 1000.f;
			u::log("switched screen, updated vsync_frame_time. refresh rate: {:.2f} hz", rate);
		}
	}
}

bool sdl::poll_config_reload() {
	// throttle: statting the file every frame would be wasteful
	Uint64 now = SDL_GetTicks();
	if (now - last_config_check_ms < CONFIG_POLL_INTERVAL_MS)
		return false;
	last_config_check_ms = now;

	std::error_code ec;
	auto write_time = std::filesystem::last_write_time(config_app::get_app_config_path(), ec);
	if (ec)
		return false; // file missing/unreadable, nothing to do

	if (has_config_write_time && write_time == last_config_write)
		return false; // unchanged since we last looked

	auto config = config_app::get_app_config();
	bool needs_redraw = apply_app_config(config);
	remember_config_write_time(); // note: store modified time after get_app_config, since parsing can rewrite the file

	// only force a redraw if something visible actually changed
	return needs_redraw;
}
