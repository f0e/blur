#pragma once

#include "config.h"

#include <vector>
#include <thread>

class c_render {
public:
	std::string video_name;
	std::string video_path;
	std::string video_folder;
	
	std::string input_filename;
	std::string output_filename;

	std::string temp_path;

public:
	s_blur_settings settings;
	std::string preview_filename;

public:
	void render();
};

class c_rendering {
private:
	std::unique_ptr<std::thread> thread_ptr;

public:
	std::vector<c_render> queue;
	bool in_render;

public:
	std::string get_ffmpeg_command(const s_blur_settings& settings, const std::string& output_name, const std::string& preview_name);

	void queue_render(c_render render);

	void start();
	void stop();
};

inline c_rendering rendering;