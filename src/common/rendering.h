#pragma once

#include "config.h"

#include <vector>
#include <thread>
#include <filesystem>

struct RenderStatus {
	bool finished = false;
	bool init = false;
	int current_frame;
	int total_frames;
	std::chrono::steady_clock::time_point start_time;
	std::chrono::duration<double> elapsed_time;
	float fps;

	void update_progress_string(bool first);
	std::string progress_string;
};

class Render {
private:
	RenderStatus m_status;

	std::wstring m_video_name;

	std::filesystem::path m_video_path;
	std::filesystem::path m_video_folder;

	std::filesystem::path m_output_path;
	std::filesystem::path m_temp_path;
	std::filesystem::path m_preview_path;

	BlurSettings m_settings;

	void build_output_filename();

	struct RenderCommands {
		std::wstring vspipe_path;
		std::vector<std::wstring> vspipe;

		std::wstring ffmpeg_path;
		std::vector<std::wstring> ffmpeg;
	};

	RenderCommands build_render_commands();

	bool do_render(RenderCommands render_commands);

public:
	Render(
		std::filesystem::path input_path,
		const std::optional<std::filesystem::path>& output_path = {},
		const std::optional<std::filesystem::path>& config_path = {}
	);

	bool create_temp_path();
	bool remove_temp_path();

	void render();

	[[nodiscard]] std::wstring get_video_name() const {
		return m_video_name;
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
};

class Rendering {
private:
	std::unique_ptr<std::thread> m_thread_ptr;
	std::optional<std::function<void()>> m_progress_callback;
	std::vector<std::unique_ptr<Render>> m_queue;
	Render* m_current_render = nullptr;

	std::mutex m_lock;

public:
	void render_videos();

	Render& queue_render(Render&& render);

	void stop_rendering();

	const std::vector<std::unique_ptr<Render>>& get_queue() {
		return m_queue;
	}

	const Render* get_current_render() {
		return m_current_render;
	}

	void set_progress_callback(std::function<void()>&& callback) {
		m_progress_callback = std::move(callback);
	}

	void run_callbacks() {
		if (m_progress_callback)
			(*m_progress_callback)();
	}

	void lock() {
		m_lock.lock();
	}

	void unlock() {
		m_lock.unlock();
	}
};

inline Rendering rendering;
