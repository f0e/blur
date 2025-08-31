#pragma once

#include <mpv/client.h>
#include <mpv/render_gl.h>

class VideoPlayer {
public:
	VideoPlayer()
		: m_wakeup_on_mpv_render_update(SDL_RegisterEvents(1)), m_wakeup_on_mpv_events(SDL_RegisterEvents(1)) {
		if (m_wakeup_on_mpv_render_update == static_cast<Uint32>(-1) ||
		    m_wakeup_on_mpv_events == static_cast<Uint32>(-1))
		{
			throw std::runtime_error("Could not register SDL events");
		}

		initialize_mpv();
	}

	VideoPlayer(const VideoPlayer&) = default;
	VideoPlayer(VideoPlayer&&) = delete;
	VideoPlayer& operator=(const VideoPlayer&) = default;
	VideoPlayer& operator=(VideoPlayer&&) = delete;

	~VideoPlayer();

	void handle_key_press(SDL_Keycode key);

	void load_file(const char* file_path);

	void render(int w, int h);

	void handle_mpv_event(const SDL_Event& event, bool& redraw);

	GLuint m_fbo;
	GLuint m_tex;

private:
	mpv_handle* m_mpv = nullptr;
	mpv_render_context* m_mpv_gl = nullptr;
	Uint32 m_wakeup_on_mpv_render_update;
	Uint32 m_wakeup_on_mpv_events;

	void initialize_mpv();

	void on_mpv_events();

	void on_mpv_render_update();

	void process_mpv_events();
};
