#pragma once

#include "config.h"

class c_ffmpeg {
public:
	std::string get_settings(std::string video_name, std::string output_name, const blur_settings& settings);
};

extern c_ffmpeg ffmpeg;