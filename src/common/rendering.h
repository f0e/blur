#pragma once

#include "config_blur.h"
#include "config_app.h"

struct RenderCommands {
	std::vector<std::string> vspipe;
	std::vector<std::string> ffmpeg;
};

struct RenderResult {
	bool stopped;
};

struct RenderStatus {
	bool finished = false;

	bool init_frames = false;
	int current_frame = 0;
	int total_frames = 0;

	bool init_fps = false;
	std::chrono::steady_clock::time_point start_time;
	int start_frame = 0;
	std::chrono::duration<double> elapsed_time;
	float fps = 0.f;

	void update_progress_string(bool first);
	void on_pause();

	std::string progress_string;
};

class Render {
private:
	uint32_t m_render_id;

	RenderStatus m_status;

	std::string m_video_name;

	std::filesystem::path m_video_path;
	std::filesystem::path m_video_folder;

	std::filesystem::path m_output_path;
	std::filesystem::path m_temp_path;
	std::filesystem::path m_preview_path;

	u::VideoInfo m_video_info;

	BlurSettings m_settings;
	bool m_is_global_config = false;

	GlobalAppSettings m_app_settings;

	bool m_to_kill = false;
	bool m_paused = false;
	int m_vspipe_pid = -1;
	int m_ffmpeg_pid = -1;

	void build_output_filename();

	tl::expected<RenderCommands, std::string> build_render_commands();

	void update_progress(int current_frame, int total_frames);

	tl::expected<RenderResult, std::string> do_render(RenderCommands render_commands);

public:
	Render(
		std::filesystem::path input_path,
		u::VideoInfo video_info,
		const std::optional<std::filesystem::path>& output_path = {},
		const std::optional<std::filesystem::path>& config_path = {}
	);

	bool operator==(const Render& other) const {
		return m_render_id == other.m_render_id;
	}

	bool create_temp_path();
	bool remove_temp_path();

	tl::expected<RenderResult, std::string> render();

	void pause();
	void resume();

	[[nodiscard]] bool is_paused() const {
		return m_paused;
	}

	void stop() {
		m_to_kill = true;
	}

	[[nodiscard]] uint32_t get_render_id() const {
		return m_render_id;
	}

	[[nodiscard]] std::string get_video_name() const {
		return m_video_name;
	}

	[[nodiscard]] std::filesystem::path get_input_video_path() const {
		return m_video_path;
	}

	[[nodiscard]] std::filesystem::path get_output_video_path() const {
		return m_output_path;
	}

	[[nodiscard]] BlurSettings get_settings() const {
		return m_settings;
	}

	[[nodiscard]] RenderStatus get_status() const {
		return m_status;
	}

	[[nodiscard]] std::filesystem::path get_preview_path() const {
		return m_preview_path;
	}

	[[nodiscard]] bool is_global_config() const {
		return m_is_global_config;
	}
};

class Rendering {
private:
	std::unique_ptr<std::thread> m_thread_ptr;
	std::vector<std::unique_ptr<Render>> m_queue;
	std::optional<uint32_t> m_current_render_id;

	std::optional<std::function<void()>> m_progress_callback;
	std::optional<std::function<void(Render*, tl::expected<RenderResult, std::string>)>> m_render_finished_callback;

	std::mutex m_lock;

public:
	bool render_next_video();

	Render& queue_render(Render&& render);

	void stop_renders_and_wait();

	const std::vector<std::unique_ptr<Render>>& get_queue() {
		return m_queue;
	}

	std::optional<Render*> get_current_render() {
		for (const auto& render : m_queue) {
			if (render->get_render_id() == m_current_render_id)
				return { render.get() };
		}

		return {};
	}

	std::optional<uint32_t> get_current_render_id() {
		return m_current_render_id;
	}

	void set_progress_callback(std::function<void()>&& callback) {
		m_progress_callback = std::move(callback);
	}

	void set_render_finished_callback(
		std::function<void(Render*, const tl::expected<RenderResult, std::string>& result)>&& callback
	) {
		m_render_finished_callback = std::move(callback);
	}

	void call_progress_callback() {
		if (m_progress_callback)
			(*m_progress_callback)();
	}

	void call_render_finished_callback(Render* render, const tl::expected<RenderResult, std::string>& result) {
		if (m_render_finished_callback)
			(*m_render_finished_callback)(render, result);
	}

	void lock() {
		m_lock.lock();
	}

	void unlock() {
		m_lock.unlock();
	}
};

inline Rendering rendering;
