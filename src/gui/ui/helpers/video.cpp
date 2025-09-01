#include "video.h"

VideoPlayer::~VideoPlayer() {
	if (m_mpv_gl) {
		mpv_render_context_free(m_mpv_gl);
	}

	if (m_mpv) {
		mpv_destroy(m_mpv);
	}

	u::log("Player properly terminated");
}

void VideoPlayer::handle_key_press(SDL_Keycode key) {
	if (key == SDLK_SPACE) {
		static std::array<const char*, 3> cmd = { "cycle", "pause", nullptr };
		mpv_command_async(m_mpv, 0, cmd.data());
	}
}

void VideoPlayer::load_file(const char* file_path) {
	static std::array<const char*, 3> cmd = { "loadfile", file_path, nullptr };
	mpv_command_async(m_mpv, 0, cmd.data());
}

void VideoPlayer::gen_fbo_texture() {
	glGenFramebuffers(1, &m_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

	glGenTextures(1, &m_tex);
	glBindTexture(GL_TEXTURE_2D, m_tex);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void VideoPlayer::setup_fbo_texture(int w, int h) const {
	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
	glBindTexture(GL_TEXTURE_2D, m_tex);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoPlayer::render(int w, int h) {
	setup_fbo_texture(w, h);

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

	mpv_render_context_render(m_mpv_gl, params.data());
}

void VideoPlayer::handle_mpv_event(const SDL_Event& event, bool& redraw) {
	if (event.type == m_wakeup_on_mpv_render_update) {
		uint64_t flags = mpv_render_context_update(m_mpv_gl);
		if (flags & MPV_RENDER_UPDATE_FRAME) {
			redraw = true;
		}
	}

	if (event.type == m_wakeup_on_mpv_events) {
		process_mpv_events();
	}
}

void VideoPlayer::initialize_mpv() {
	m_mpv = mpv_create();
	if (!m_mpv) {
		throw std::runtime_error("MPV context creation failed");
	}

	// configure mpv
	mpv_set_option_string(m_mpv, "vo", "libmpv");

	if (mpv_initialize(m_mpv) < 0) {
		throw std::runtime_error("MPV initialization failed");
	}

	mpv_request_log_messages(m_mpv, "debug");

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
	if (mpv_render_context_create(&m_mpv_gl, m_mpv, params.data()) < 0) {
		throw std::runtime_error("Failed to initialize MPV GL context");
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

void VideoPlayer::on_mpv_events() {
	SDL_Event event = { .type = m_wakeup_on_mpv_events };
	SDL_PushEvent(&event);
}

void VideoPlayer::on_mpv_render_update() {
	SDL_Event event = { .type = m_wakeup_on_mpv_render_update };
	SDL_PushEvent(&event);
}

void VideoPlayer::process_mpv_events() {
	// handle all pending mpv events
	while (true) {
		mpv_event* mp_event = mpv_wait_event(m_mpv, 0);

		if (mp_event->event_id == MPV_EVENT_NONE) {
			break;
		}

		if (mp_event->event_id == MPV_EVENT_LOG_MESSAGE) {
			auto* msg = static_cast<mpv_event_log_message*>(mp_event->data);
			// print specific debug log messages
			if (std::strstr(msg->text, "DR image")) {
				u::log("Log: {}", msg->text);
			}
			continue;
		}

		u::log("Event: ", mpv_event_name(mp_event->event_id));
	}
}
