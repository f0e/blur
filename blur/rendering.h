#pragma once

#include "config.h"

#include <vector>
#include <thread>

class c_render {
private:
	std::string video_name;
	std::string video_path;
	std::string video_folder;
	
	std::string input_filename;
	std::string output_path;

	std::string temp_path;

	s_blur_settings settings;
	std::string preview_filename;

private:
	void build_output_filename();
	std::string build_ffmpeg_command();

public:
	c_render(const std::string& input_path, std::optional<std::string> output_filename = {}, std::optional<std::string> config_path = {});

	std::string get_video_name() {
		return video_name;
	}

	std::string get_output_video_path() {
		return output_path;
	}

public:
	void render();
};

class c_rendering {
private:
	std::unique_ptr<std::thread> thread_ptr;

public:
	std::vector<c_render> queue;
	bool renders_queued;

public:
	void render_videos();
	void render_videos_thread();

	void queue_render(c_render render);

	void start_thread();
	void stop_thread();
};

inline c_rendering rendering;