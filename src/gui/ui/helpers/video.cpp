#include "video.h"

const int SEEK_SECS = 3;

VideoPlayer::~VideoPlayer() {
	m_thread_exit = true;
	if (m_mpv_thread.joinable())
		m_mpv_thread.join();
	m_seek_cv.notify_one();

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
		// libmpv's wasapi output calls CoUninitialize on whatever thread tears the player down, without
		// a matching init. sdl set this thread up as an OLE apartment and registered the window's
		// IDropTarget on it, so previewing a few videos with audio walks that refcount down to zero: the
		// apartment dies, the now-dead drop target is left registered on the window, and dropping files
		// onto blur gives you a "no" cursor for the rest of the session. destroy off this thread so the
		// stray call lands somewhere that has no apartment to lose.
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
			run_command_async({ "frame-step", "1", "seek" });
			break;
		}

		default:
			break;
	}
}

void VideoPlayer::reset_loaded_file() {
	m_loaded_file = {};
	m_is_seeking = false;

	m_cached_percent_pos = -1.0;
	m_cached_duration = -1.0;
	m_cached_fps = 0.0;
	m_cached_pause = true;
	m_cached_width = 0;
	m_cached_height = 0;
}

void VideoPlayer::load_file(const std::filesystem::path& file_path) {
	run_command_async({ "loadfile", u::path_to_string(file_path) });

	m_current_file_path = file_path;
	reset_loaded_file();
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
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

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

void VideoPlayer::handle_mpv_event(const SDL_Event& event, bool& redraw, bool should_render) {
	if (should_render && event.type == m_wakeup_on_mpv_render_update) {
		uint64_t flags = mpv_render_context_update(m_mpv_gl);
		if (flags & MPV_RENDER_UPDATE_FRAME) {
			redraw = true;
			m_new_frame_available = true;
		}
	}

	if (event.type == m_wakeup_on_mpv_events) {
		process_mpv_events();
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
	mpv_set_option_string(m_mpv, "hwdec", "yes");
	mpv_set_option_string(m_mpv, "profile", "fast");
	mpv_set_option_string(m_mpv, "keep-open", "yes"); // dont close when finished
	mpv_set_option_string(m_mpv, "pause", "yes");
	mpv_set_option_string(m_mpv, "volume", std::format("{:.2f}", volume).c_str());
	// mpv_set_option_string(m_mpv, "mute", "yes");
	//
	mpv_observe_property(m_mpv, 0, "percent-pos", MPV_FORMAT_DOUBLE);
	mpv_observe_property(m_mpv, 0, "duration/full", MPV_FORMAT_DOUBLE);
	mpv_observe_property(m_mpv, 0, "container-fps", MPV_FORMAT_DOUBLE);
	mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
	mpv_observe_property(m_mpv, 0, "dwidth", MPV_FORMAT_INT64);
	mpv_observe_property(m_mpv, 0, "dheight", MPV_FORMAT_INT64);

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
			return (void*)SDL_GL_GetProcAddress(name);
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

	std::vector<mpv_render_param> params{ { .type = MPV_RENDER_PARAM_API_TYPE,
		                                    .data = (char*)(MPV_RENDER_API_TYPE_OPENGL) },
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

	m_thread_exit = false;
	m_mpv_thread = std::thread(&VideoPlayer::mpv_thread, this);
}

void VideoPlayer::mpv_thread() {
	while (!blur.exiting && !m_thread_exit) {
		std::unique_lock<std::mutex> lock(m_mutex);

		m_seek_cv.wait_for(lock, std::chrono::milliseconds(50), [this] {
			return (m_queued_seek.has_value() && !m_is_seeking) || m_thread_exit || blur.exiting;
		});

		if (!m_mpv || !m_loaded_file) {
			continue;
		}

		if (m_queued_seek && !m_is_seeking) {
			auto seek = *m_queued_seek;
			m_queued_seek.reset();
			m_is_seeking = true;
			lock.unlock();

			std::string flags = "absolute-percent";
			if (seek.exact)
				flags += "+exact";

			run_command({ "seek", std::to_string(seek.time * 100), flags });

			// mpv_set_property_async(m_mpv, 0, "percent-pos", MPV_FORMAT_DOUBLE, &seek_to);
		}
	}
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

void VideoPlayer::process_mpv_events() {
	// handle all pending mpv events
	while (true) {
		mpv_event* mp_event = mpv_wait_event(m_mpv, 0);

		if (mp_event->event_id == MPV_EVENT_NONE) {
			break;
		}

		switch (mp_event->event_id) {
			case MPV_EVENT_LOG_MESSAGE: {
				auto* msg = static_cast<mpv_event_log_message*>(mp_event->data);
				if (std::strstr(msg->text, "DR image")) {
					u::log("MPV Log: {}", msg->text);
				}
				break;
			}
			case MPV_EVENT_START_FILE: {
				u::log("MPV: Starting file");
				m_loaded_file = {};
				m_is_seeking = false;
				break;
			}
			case MPV_EVENT_FILE_LOADED: {
				u::log("MPV: File loaded");
				break;
			}
			case MPV_EVENT_VIDEO_RECONFIG: {
				u::log("MPV: Video reconfigured");
				// video is now ready to render
				m_loaded_file = m_current_file_path;
				break;
			}
			case MPV_EVENT_PLAYBACK_RESTART: {
				u::log("MPV: Playback restarted");
				m_is_seeking = false;
				m_seek_cv.notify_one();
				break;
			}
			case MPV_EVENT_END_FILE: {
				auto* end_event = static_cast<mpv_event_end_file*>(mp_event->data);
				if (end_event->reason == MPV_END_FILE_REASON_ERROR) {
					u::log_error("MPV: File ended with error: {}", mpv_error_string(end_event->error));
				}
				else {
					u::log("MPV: File ended normally");
				}
				m_loaded_file = {};
				break;
			}
			case MPV_EVENT_PROPERTY_CHANGE: {
				auto* prop = static_cast<mpv_event_property*>(mp_event->data);

				if (prop->format == MPV_FORMAT_NONE)
					break;

				const char* name = prop->name;

				if (std::strcmp(name, "percent-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
					m_cached_percent_pos = *static_cast<double*>(prop->data);
				}
				else if (std::strcmp(name, "duration/full") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
					m_cached_duration = *static_cast<double*>(prop->data);

					// note: this might not be the 'correct' place to do this, but it needs to be called once
					// as early as possible & requires the duration to be loaded.
					// since duration is set here and it should only happen once, seems good enough.
					update_playback_range();
				}
				else if (std::strcmp(name, "container-fps") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
					m_cached_fps = *static_cast<double*>(prop->data);
				}
				else if (std::strcmp(name, "pause") == 0 && prop->format == MPV_FORMAT_FLAG) {
					m_cached_pause = *static_cast<int*>(prop->data);
				}
				else if (std::strcmp(name, "dwidth") == 0 && prop->format == MPV_FORMAT_INT64) {
					m_cached_width = *static_cast<int64_t*>(prop->data);
				}
				else if (std::strcmp(name, "dheight") == 0 && prop->format == MPV_FORMAT_INT64) {
					m_cached_height = *static_cast<int64_t*>(prop->data);
				}
				break;
			}
			default: {
				u::log("MPV Event: {}", mpv_event_name(mp_event->event_id));
				break;
			}
		}
	}
}
