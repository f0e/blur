#pragma once

#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <utility>
#include <optional>

struct Seek {
	float time;
	bool exact;

	bool operator==(const Seek& other) const = default;
};

class VideoPlayer {
public:
	VideoPlayer(float volume, bool hardware_decoding)
		: m_wakeup_on_mpv_render_update(SDL_RegisterEvents(1)), m_wakeup_on_mpv_events(SDL_RegisterEvents(1)),
		  m_hardware_decoding(hardware_decoding) {
		if (m_wakeup_on_mpv_render_update == static_cast<Uint32>(-1) ||
		    m_wakeup_on_mpv_events == static_cast<Uint32>(-1))
		{
			throw std::runtime_error("Could not register SDL events");
		}

		initialize_mpv(volume);
		gen_fbo_texture();
	}

	VideoPlayer(const VideoPlayer&) = delete; // should not be copyable
	VideoPlayer(VideoPlayer&&) = delete;
	VideoPlayer& operator=(const VideoPlayer&) = delete;
	VideoPlayer& operator=(VideoPlayer&&) = delete;

	~VideoPlayer();

	void handle_key_press(SDL_Keycode key);

	void load_file(const std::filesystem::path& file_path);
	void stop();

	bool render(int w, int h);

	[[nodiscard]] GLuint get_frame_texture_for_render() const {
		return m_tex;
	}

	void handle_mpv_event(const SDL_Event& event, bool& redraw, bool should_render);

	[[nodiscard]] std::optional<std::pair<int, int>> get_video_dimensions() const {
		if (m_cached_width > 0 && m_cached_height > 0)
			return std::make_pair(static_cast<int>(m_cached_width.load()), static_cast<int>(m_cached_height.load()));

		return {};
	}

	[[nodiscard]] std::optional<float> get_percent_pos() const {
		if (m_cached_percent_pos >= 0.0)
			return static_cast<float>(m_cached_percent_pos.load());

		return {};
	}

	[[nodiscard]] bool is_seeking() const {
		return m_is_seeking;
	}

	[[nodiscard]] std::optional<double> get_fps() const {
		if (m_cached_fps >= 0.0)
			return m_cached_fps.load();
		return {};
	}

	[[nodiscard]] std::optional<bool> get_paused() const {
		return m_cached_pause.load();
	}

	[[nodiscard]] std::optional<double> get_duration() const {
		if (m_cached_duration >= 0.0)
			return m_cached_duration.load();
		return {};
	}

	[[nodiscard]] bool is_video_ready() const {
		return m_loaded_file.has_value();
	}

	void seek(float time, bool exact) {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_queued_seek = Seek{
				.time = time,
				.exact = exact,
			};
			m_cached_percent_pos = -1.0;
		}
		m_seek_cv.notify_one();
	}

	void set_hardware_decoding(bool hardware_decoding) {
		if (hardware_decoding == m_hardware_decoding)
			return;

		m_hardware_decoding = hardware_decoding;

		run_command_async({ "set", "hwdec", hwdec_option(hardware_decoding) });
	}

	void set_paused(bool paused) {
		run_command_async({ "set", "pause", paused ? "yes" : "no" });
	}

	void cycle_paused() {
		run_command_async({ "cycle", "pause" });
	}

	void set_playback_range(float start, float end) {
		auto fps = get_fps();
		if (!fps)
			return;

		// @note:extra-frame visually it seems like the end of the cut is included. so include it
		end = end + (1.0 / *fps);

		run_command_async({ "set", "ab-loop-a", std::to_string(start) });
		run_command_async({ "set", "ab-loop-b", std::to_string(end) });
	}

	void reset_playback_range() {
		run_command_async({ "del", "ab-loop-a" });
		run_command_async({ "del", "ab-loop-b" });
	}

	void update_playback_range() {
		auto duration = get_duration();
		if (duration)
			set_playback_range(m_start_percent * *duration, m_end_percent * *duration);
	}

	void set_end(float percent) {
		if (percent == m_end_percent)
			return;

		m_end_percent = percent;

		update_playback_range();
	}

	void set_start(float percent) {
		if (percent == m_start_percent)
			return;

		m_start_percent = percent;

		update_playback_range();
	}

	std::optional<Seek> get_queued_seek() {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queued_seek;
	}

	std::optional<std::filesystem::path> get_current_file_path() {
		return m_current_file_path;
	}

	// std::optional<Seek> get_seek() {
	// 	std::lock_guard<std::mutex> lock(m_mutex);
	// 	if (m_queued_seek)
	// 		return m_queued_seek;
	// 	return m_last_seek;
	// }

private:
	mpv_handle* m_mpv = nullptr;
	mpv_render_context* m_mpv_gl = nullptr;
	Uint32 m_wakeup_on_mpv_render_update;
	Uint32 m_wakeup_on_mpv_events;

	GLuint m_fbo{};
	GLuint m_tex{};

	int m_current_width{};
	int m_current_height{};

	std::optional<std::filesystem::path> m_current_file_path;
	std::optional<std::filesystem::path> m_loaded_file;

	bool m_hardware_decoding;

	std::mutex m_mutex;
	std::condition_variable m_seek_cv;
	std::optional<Seek> m_queued_seek;

	std::thread m_mpv_thread;
	std::atomic<bool> m_thread_exit{ false };
	std::atomic<bool> m_is_seeking{ false };
	std::atomic<bool> m_new_frame_available{ false };
	std::atomic<double> m_cached_percent_pos{ -1.0 };
	std::atomic<double> m_cached_duration{ -1.0 };
	std::atomic<double> m_cached_fps{ -1.0 };
	std::atomic<bool> m_cached_pause{ true };
	std::atomic<double> m_cached_width{ -1.0 };
	std::atomic<double> m_cached_height{ -1.0 };

	float m_start_percent = 0.f;
	float m_end_percent = 1.f;

	static const char* hwdec_option(bool hardware_decoding) {
		return hardware_decoding ? "yes" : "no";
	}

	void initialize_mpv(float volume);

	void mpv_thread();

	void on_mpv_events();

	void on_mpv_render_update();

	void process_mpv_events();

	void gen_fbo_texture();

	void setup_fbo_texture(int w, int h);

	void reset_loaded_file();

	template<typename VariableType>
	std::optional<VariableType> get_property(const std::string& key, mpv_format variable_format) const {
		if (!m_mpv || !m_loaded_file)
			return {};

		VariableType data = 0;
		int res = mpv_get_property(m_mpv, key.c_str(), variable_format, &data);

		if (res != 0)
			return {};

		return data;
	}

	void run_command_impl(const std::vector<std::string>& command, bool async) {
		if (!m_mpv)
			return;

		std::vector<const char*> cmd;
		cmd.reserve(command.size() + 1);

		for (const auto& s : command) {
			cmd.push_back(s.c_str());
		}
		cmd.push_back(nullptr);

		if (async) {
			mpv_command_async(m_mpv, 0, cmd.data());
		}
		else {
			mpv_command(m_mpv, cmd.data());
		}
	}

	void run_command_async(const std::vector<std::string>& command) {
		run_command_impl(command, true);
	}

	void run_command(const std::vector<std::string>& command) {
		run_command_impl(command, false);
	}
};
