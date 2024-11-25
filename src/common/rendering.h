#pragma once

#include "config.h"

#include <vector>
#include <thread>
#include <filesystem>

struct s_render_status {
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

class c_render {
private:
	s_render_status status;

	std::wstring video_name;

	std::filesystem::path video_path;
	std::filesystem::path video_folder;

	std::filesystem::path output_path;
	std::filesystem::path temp_path;
	std::filesystem::path preview_path;

	s_blur_settings settings;

private:
	void build_output_filename();

	struct s_render_commands {
		std::wstring vspipe_path;
		std::vector<std::wstring> vspipe;

		std::wstring ffmpeg_path;
		std::vector<std::wstring> ffmpeg;
	};

	s_render_commands build_render_commands();

	bool do_render(s_render_commands render_commands);

public:
	c_render(
		const std::filesystem::path& input_path,
		std::optional<std::filesystem::path> output_path = {},
		std::optional<std::filesystem::path> config_path = {}
	);

	bool create_temp_path();
	bool remove_temp_path();

	std::wstring get_video_name() {
		return video_name;
	}

	std::filesystem::path get_output_video_path() {
		return output_path;
	}

	s_blur_settings get_settings() {
		return settings;
	}

public:
	void render();

	s_render_status get_status() {
		return status;
	}

	std::filesystem::path get_preview_path() {
		return preview_path;
	}
};

class c_rendering {
private:
	std::unique_ptr<std::thread> thread_ptr;

public:
	std::vector<std::shared_ptr<c_render>> queue;
	std::shared_ptr<c_render> current_render;
	std::optional<std::function<void()>> progress_callback;
	bool renders_queued;

public:
	void render_videos();

	void queue_render(std::shared_ptr<c_render> render);

	void stop_rendering();

	void run_callbacks() {
		if (progress_callback)
			(*progress_callback)();
	}
};

inline c_rendering rendering;
