#include "video.h"
#include "../../render/render.h"

const int SEEK_SECS = 3;
const uint64_t SEEK_REPLY_ID = 1;
const uint64_t DIMENSIONS_OBSERVE_ID = 1;

VideoPlayer::~VideoPlayer() {
	// clean up opengl resources
	if (m_tex) {
		glDeleteTextures(1, &m_tex);
		m_tex = 0;
	}
	if (m_fbo) {
		glDeleteFramebuffers(1, &m_fbo);
		m_fbo = 0;
	}

	if (m_mpv_gl) {
		mpv_render_context_free(m_mpv_gl);
	}

	if (m_mpv) {
		// libmpv's wasapi output calls CoUninitialize on the thread that destroys it, which eventually kills sdl's ole
		// apartment and breaks drag and drop. destroy it on another thread instead
		std::thread destroy_thread([mpv = m_mpv] {
			mpv_destroy(mpv);
		});
		destroy_thread.join();
	}

	u::log("Player properly terminated");
}

void VideoPlayer::handle_key_press(SDL_Keycode key) {
	switch (key) {
		case SDLK_SPACE: {
			cycle_paused();
			break;
		}

		case SDLK_LEFT: {
			run_command_async({ "seek", std::format("-{}", SEEK_SECS) });
			break;
		}

		case SDLK_RIGHT: {
			run_command_async({ "seek", std::format("{}", SEEK_SECS) });
			break;
		}

		case SDLK_COMMA: {
			run_command_async({ "frame-back-step" });
			break;
		}

		case SDLK_PERIOD: {
			// the seek flag doesn't move forward. this plays one frame, with the audio muted, then pauses again
			m_frame_step_time = std::chrono::steady_clock::now();
			run_command_async({ "frame-step", "1", "mute" });
			break;
		}

		default:
			break;
	}
}

void VideoPlayer::observe_dimensions() {
	mpv_unobserve_property(m_mpv, DIMENSIONS_OBSERVE_ID);

	mpv_observe_property(m_mpv, DIMENSIONS_OBSERVE_ID, "dwidth", MPV_FORMAT_INT64);
	mpv_observe_property(m_mpv, DIMENSIONS_OBSERVE_ID, "dheight", MPV_FORMAT_INT64);
}

void VideoPlayer::reset_loaded_file() {
	m_loaded_file = {};
	m_is_seeking = false;
	m_awaiting_seek_frame = false;
	m_has_frame = false;

	m_cached_percent_pos = -1.0;
	m_cached_time_pos = -1.0;
	m_cached_duration = -1.0;
	m_cached_fps = 0.0;
	m_cached_width = 0;
	m_cached_height = 0;

	// mpv only sends a property when it changes, and the next file can be the same size. observing again sends the
	// current size, so it isn't left cleared
	if (m_mpv)
		observe_dimensions();
}

void VideoPlayer::load_file(
	const std::filesystem::path& file_path,
	std::optional<float> start_time,
	const std::vector<std::pair<std::string, std::string>>& options
) {
	// opening at the start time rather than seeking after means frame 0 is never shown
	if (start_time)
		run_command_async({ "set", "start", std::to_string(*start_time) });

	if (options.empty()) {
		run_command_async({ "loadfile", u::path_to_string(file_path) });
	}
	else {
		std::string option_list;
		for (const auto& [key, value] : options) {
			if (!option_list.empty())
				option_list += ",";

			// %length% quoting lets values contain commas
			option_list += std::format("{}=%{}%{}", key, value.size(), value);
		}

		run_command_async({ "loadfile", u::path_to_string(file_path), "replace", "-1", option_list });
	}

	m_current_file_path = file_path;
	reset_loaded_file();
}

void VideoPlayer::screenshot_to_file(
	const std::filesystem::path& path, std::function<void(std::optional<std::string> error)> on_done
) {
	uint64_t reply_id = m_next_reply_id++;
	m_reply_callbacks[reply_id] = std::move(on_done);

	run_command_async({ "screenshot-to-file", u::path_to_string(path), "video" }, reply_id);
}

void VideoPlayer::stop() {
	run_command_async({ "stop" });

	m_current_file_path = {};
	reset_loaded_file();
}

void VideoPlayer::gen_fbo_texture() {
	glGenFramebuffers(1, &m_fbo);
	glGenTextures(1, &m_tex);

	glBindTexture(GL_TEXTURE_2D, m_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// initialize with a default size
	m_current_width = 0;
	m_current_height = 0;
}

void VideoPlayer::setup_fbo_texture(int w, int h) {
	// only recreate texture if dimensions changed
	if (w != m_current_width || h != m_current_height) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
		glBindTexture(GL_TEXTURE_2D, m_tex);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

		// check framebuffer completeness
		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			throw std::runtime_error("Framebuffer not complete");
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, 0);

		m_current_width = w;
		m_current_height = h;

		if (glGetError() != GL_NO_ERROR) {
			throw std::runtime_error("OpenGL error during FBO setup");
		}
	}
}

bool VideoPlayer::render(int w, int h) {
	// don't render if we don't have valid dimensions
	if (w <= 0 || h <= 0) {
		return false;
	}

	// don't render until video is actually loaded and ready
	if (!m_loaded_file) {
		return false;
	}

	bool size_changed = (w != m_current_width || h != m_current_height);
	setup_fbo_texture(w, h);

	if (!m_new_frame_available && !size_changed)
		return true;

	m_new_frame_available = false;

	// save current opengl state
	GLint prev_fbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

	// clear the framebuffer
	glViewport(0, 0, w, h);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	mpv_opengl_fbo fbo{
		.fbo = (int)m_fbo,
		.w = w,
		.h = h,
		.internal_format = GL_RGB,
	};

	int flip_y = 0;

	std::vector<mpv_render_param> params{ { .type = MPV_RENDER_PARAM_OPENGL_FBO, .data = &fbo },
		                                  { .type = MPV_RENDER_PARAM_FLIP_Y, .data = &flip_y },
		                                  { .type = MPV_RENDER_PARAM_INVALID } };

	int result = mpv_render_context_render(m_mpv_gl, params.data());
	if (result < 0) {
		u::log_error("MPV render failed: {}", mpv_error_string(result));
		// restore previous framebuffer
		glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
		return false;
	}

	// restore previous framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
	return true;
}

bool VideoPlayer::draw(const gfx::Rect& rect, const gfx::Color& tint) {
	int w = (int)std::lround(rect.w * render::framebuffer_scale);
	int h = (int)std::lround(rect.h * render::framebuffer_scale);

	if (!render(w, h))
		return false;

	render::imgui.drawlist->AddImage(
		(ImTextureID)(intptr_t)m_tex, rect.origin(), rect.max(), ImVec2(0, 0), ImVec2(1, 1), tint.to_imgui()
	);

	return true;
}

void VideoPlayer::handle_mpv_event(const SDL_Event& event, bool& redraw, bool should_render) {
	if (should_render && event.type == m_wakeup_on_mpv_render_update) {
		uint64_t flags = mpv_render_context_update(m_mpv_gl);
		if (flags & MPV_RENDER_UPDATE_FRAME) {
			redraw = true;
			m_new_frame_available = true;
			m_has_frame = true;

			// a seek resets the output, which asks for the old frame to be redrawn before the new one arrives
			mpv_render_frame_info info{};
			mpv_render_context_get_info(m_mpv_gl, { .type = MPV_RENDER_PARAM_NEXT_FRAME_INFO, .data = &info });
			if (!(info.flags & MPV_RENDER_FRAME_INFO_REDRAW))
				m_awaiting_seek_frame = false;
		}
	}

	if (event.type == m_wakeup_on_mpv_events) {
		if (process_mpv_events())
			redraw = true;
	}
}

void VideoPlayer::initialize_mpv(float volume) {
	m_mpv = mpv_create();
	if (!m_mpv) {
		throw std::runtime_error("MPV context creation failed");
	}

	// required
	mpv_set_option_string(
		m_mpv, "vo", "libmpv"
	); // note: this is slower than gpu, but cant do anything abt it - https://github.com/mpv-player/mpv/issues/6829

	// options
	mpv_set_option_string(
		m_mpv, "hwdec", hwdec_option(m_hardware_decoding)
	); // note: hwdec isn't always faster. on mac high fps clips stutter but with no hwdec they're smooth
	mpv_set_option_string(m_mpv, "profile", "fast");
	mpv_set_option_string(m_mpv, "keep-open", "yes"); // dont close when finished
	mpv_set_option_string(m_mpv, "pause", "yes");
	mpv_set_option_string(m_mpv, "volume", std::format("{:.2f}", volume).c_str());

	mpv_observe_property(m_mpv, 0, "percent-pos", MPV_FORMAT_DOUBLE);
	mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
	mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
	mpv_observe_property(m_mpv, 0, "duration/full", MPV_FORMAT_DOUBLE);
	mpv_observe_property(m_mpv, 0, "container-fps", MPV_FORMAT_DOUBLE);
	mpv_observe_property(m_mpv, 0, "hwdec-current", MPV_FORMAT_STRING);
	observe_dimensions();

	int result = mpv_initialize(m_mpv);
	if (result < 0) {
		mpv_destroy(m_mpv);
		m_mpv = nullptr;
		throw std::runtime_error("MPV initialization failed: " + std::string(mpv_error_string(result)));
	}

	mpv_request_log_messages(m_mpv, "warn");

	// set up callbacks
	mpv_set_wakeup_callback(
		m_mpv,
		[](void* data) {
			auto* player = static_cast<VideoPlayer*>(data);
			player->on_mpv_events();
		},
		this
	);

	mpv_opengl_init_params init_params{
		.get_proc_address = [](void* ctx, const char* name) -> void* {
			return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
		},
	};

	// Tell libmpv that you will call mpv_render_context_update() on render
	// context update callbacks, and that you will _not_ block on the core
	// ever (see <libmpv/render.h> "Threading" section for what libmpv
	// functions you can call at all when this is active).
	// In particular, this means you must call e.g. mpv_command_async()
	// instead of mpv_command().
	// If you want to use synchronous calls, either make them on a separate
	// thread, or remove the option below (this will disable features like
	// DR and is not recommended anyway).
	int advanced_control = 1;

	// mpv takes a non-const pointer here but only ever reads it
	auto* api_type = const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL); // NOLINT(cppcoreguidelines-pro-type-const-cast)

	std::vector<mpv_render_param> params{ { .type = MPV_RENDER_PARAM_API_TYPE, .data = api_type },
		                                  { .type = MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, .data = &init_params },
		                                  { .type = MPV_RENDER_PARAM_ADVANCED_CONTROL, .data = &advanced_control },
		                                  { .type = MPV_RENDER_PARAM_INVALID } };

	// create mpv render context
	result = mpv_render_context_create(&m_mpv_gl, m_mpv, params.data());
	if (result < 0) {
		mpv_destroy(m_mpv);
		m_mpv = nullptr;
		throw std::runtime_error("Failed to initialize MPV GL context: " + std::string(mpv_error_string(result)));
	}

	// set up render update callback
	mpv_render_context_set_update_callback(
		m_mpv_gl,
		[](void* data) {
			auto* player = static_cast<VideoPlayer*>(data);
			player->on_mpv_render_update();
		},
		this
	);
}

void VideoPlayer::queue_seek(const Seek& seek) {
	m_queued_seek = seek;
	m_cached_percent_pos = -1.0;

	send_queued_seek();
}

// only one seek is in flight at a time, newer ones replace the queued one until it finishes
void VideoPlayer::send_queued_seek() {
	if (!m_queued_seek || m_is_seeking || !m_loaded_file)
		return;

	auto seek = *m_queued_seek;
	m_queued_seek = {};
	m_is_seeking = true;
	m_awaiting_seek_frame = true;

	std::string flags = "absolute";
	if (seek.exact)
		flags += "+exact";

	run_command_async({ "seek", std::to_string(seek.time), flags }, SEEK_REPLY_ID);
}

void VideoPlayer::on_mpv_events() {
	SDL_Event event = { .type = m_wakeup_on_mpv_events };
	if (!SDL_PushEvent(&event)) {
		u::log_error("Failed to push MPV event: {}", SDL_GetError());
	}
}

void VideoPlayer::on_mpv_render_update() {
	SDL_Event event = { .type = m_wakeup_on_mpv_render_update };
	if (!SDL_PushEvent(&event)) {
		u::log_error("Failed to push MPV render update event: {}", SDL_GetError());
	}
}

bool VideoPlayer::process_mpv_events() {
	bool changed = false;

	// handle all pending mpv events
	while (true) {
		mpv_event* mp_event = mpv_wait_event(m_mpv, 0);

		if (mp_event->event_id == MPV_EVENT_NONE) {
			break;
		}

		switch (mp_event->event_id) {
			case MPV_EVENT_LOG_MESSAGE: {
				auto* msg = static_cast<mpv_event_log_message*>(mp_event->data);
				if (msg->log_level <= MPV_LOG_LEVEL_ERROR) {
					u::log_error("MPV [{}]: {}", msg->prefix, msg->text);
					m_file_errors += msg->text;
				}
				else if (std::strstr(msg->text, "DR image")) {
					u::log("MPV Log: {}", msg->text);
				}
				break;
			}
			case MPV_EVENT_START_FILE: {
				changed = true;
				u::log("MPV: Starting file");
				m_loaded_file = {};
				m_is_seeking = false;
				m_has_frame = false;
				m_file_errors.clear();
				break;
			}
			case MPV_EVENT_FILE_LOADED: {
				u::log("MPV: File loaded");
				break;
			}
			case MPV_EVENT_VIDEO_RECONFIG: {
				changed = true;
				u::log("MPV: Video reconfigured");
				// video is now ready to render
				m_loaded_file = m_current_file_path;
				send_queued_seek();
				break;
			}
			case MPV_EVENT_PLAYBACK_RESTART: {
				changed = true;
				u::log("MPV: Playback restarted");
				m_is_seeking = false;
				send_queued_seek();
				break;
			}
			case MPV_EVENT_COMMAND_REPLY: {
				changed = true;
				// a failed seek never restarts playback, so it has to be cleared here
				auto callback = m_reply_callbacks.find(mp_event->reply_userdata);
				if (callback != m_reply_callbacks.end()) {
					auto on_done = std::move(callback->second);
					m_reply_callbacks.erase(callback);

					if (mp_event->error < 0)
						on_done(mpv_error_string(mp_event->error));
					else
						on_done(std::nullopt);
				}

				if (mp_event->reply_userdata == SEEK_REPLY_ID && mp_event->error < 0) {
					u::log_error("MPV: Seek failed: {}", mpv_error_string(mp_event->error));
					m_is_seeking = false;
					m_awaiting_seek_frame = false;
					send_queued_seek();
				}
				break;
			}
			case MPV_EVENT_END_FILE: {
				changed = true;
				auto* end_event = static_cast<mpv_event_end_file*>(mp_event->data);
				if (end_event->reason == MPV_END_FILE_REASON_ERROR) {
					u::log_error("MPV: File ended with error: {}", mpv_error_string(end_event->error));
					m_load_error = m_file_errors.empty() ? mpv_error_string(end_event->error) : m_file_errors;
				}
				else {
					u::log("MPV: File ended normally");
				}
				m_loaded_file = {};
				break;
			}
			case MPV_EVENT_PROPERTY_CHANGE: {
				auto* prop = static_cast<mpv_event_property*>(mp_event->data);

				// unavailable properties (e.g. between files) go back to their unset values
				bool available = prop->format != MPV_FORMAT_NONE;

				const char* name = prop->name;

				if (std::strcmp(name, "percent-pos") == 0) {
					m_cached_percent_pos = available ? *static_cast<double*>(prop->data) : -1.0;
				}
				else if (std::strcmp(name, "time-pos") == 0) {
					m_cached_time_pos = available ? *static_cast<double*>(prop->data) : -1.0;
				}
				else if (std::strcmp(name, "pause") == 0 && available) {
					bool paused = *static_cast<int*>(prop->data) != 0;

					// a frame step can unpause for the frame, which isn't really playing. mpv doesn't always say it
					// did, so it's only ignored just after a step
					constexpr auto FRAME_STEP_WINDOW = std::chrono::milliseconds(200);
					if (!paused && std::chrono::steady_clock::now() - m_frame_step_time < FRAME_STEP_WINDOW)
						break;

					m_paused = paused;
					changed = true;
				}
				else if (std::strcmp(name, "duration/full") == 0) {
					m_cached_duration = available ? *static_cast<double*>(prop->data) : -1.0;

					// note: this might not be the 'correct' place to do this, but it needs to be called once
					// as early as possible & requires the duration to be loaded.
					// since duration is set here and it should only happen once, seems good enough.
					update_playback_range();
				}
				else if (std::strcmp(name, "container-fps") == 0) {
					m_cached_fps = available ? *static_cast<double*>(prop->data) : 0.0;

					// the range end depends on fps too, which can arrive after the duration
					update_playback_range();
				}
				else if (std::strcmp(name, "dwidth") == 0) {
					m_cached_width = available ? *static_cast<int64_t*>(prop->data) : 0;
				}
				else if (std::strcmp(name, "dheight") == 0) {
					m_cached_height = available ? *static_cast<int64_t*>(prop->data) : 0;
				}
				else if (std::strcmp(name, "hwdec-current") == 0 && available) {
					const char* hwdec = *static_cast<char**>(prop->data);
					u::log("MPV: hardware decoder: {}", hwdec ? hwdec : "unknown");
				}
				break;
			}
			default: {
				u::log("MPV Event: {}", mpv_event_name(mp_event->event_id));
				break;
			}
		}
	}

	return changed;
}
