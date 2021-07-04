#pragma once

#include "config.h"

class c_script_handler {
public:
	std::string script_filename;

public:
	void create(const std::string& temp_path, const std::string& video_path, const s_blur_settings& settings);
};

inline c_script_handler script_handler;