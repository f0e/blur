#pragma once

#include "config.h"

class c_script_handler {
public:
	std::filesystem::path script_path;

public:
	void create(const std::filesystem::path& temp_path, const std::filesystem::path& video_path, const s_blur_settings& settings);
};

inline c_script_handler script_handler;