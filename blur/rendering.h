#pragma once

#include "config.h"

#include <vector>
#include <thread>

struct s_render_status {
	bool finished = false;
	bool init = false;
	int current_frame;
	int total_frames;
	std::chrono::steady_clock::time_point start_time;
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

	struct s_render_command {
		std::wstring pipe_command;
		std::wstring ffmpeg_command;
	};
	s_render_command build_render_command();

	bool do_render(s_render_command render_command);

public:
	c_render(const std::filesystem::path& input_path, std::optional<std::filesystem::path> output_path = {}, std::optional<std::filesystem::path> config_path = {});

	std::wstring get_video_name() {
		return video_name;
	}

	std::filesystem::path get_output_video_path() {
		return output_path;
	}

public:
	void render();

	s_render_status get_status() {
		return status;
	}
};

class c_rendering {
private:
	std::unique_ptr<std::thread> thread_ptr;

public:
	std::vector<std::shared_ptr<c_render>> queue;
	std::shared_ptr<c_render> current_render;
	bool renders_queued;

public:
	PROCESS_INFORMATION vspipe_pi;
	PROCESS_INFORMATION ffmpeg_pi;

	void render_videos();

	void queue_render(std::shared_ptr<c_render> render);

	void stop_rendering();
};

inline c_rendering rendering;