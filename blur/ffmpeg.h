#pragma once

#include "config.h"

class c_ffmpeg {
public:
	std::string get_settings(const blur_settings& settings, const std::string& output_name, const std::string& preview_name);
};

extern c_ffmpeg ffmpeg;