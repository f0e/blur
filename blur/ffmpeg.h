#pragma once

#include <string>

class c_ffmpeg {
public:
	std::string get_settings(std::string video_name, std::string output_name);
};

extern c_ffmpeg ffmpeg;