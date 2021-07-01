#pragma once

#include "config.h"

class c_avisynth_handler {
private:
	std::string filename;

public:
	void create(const std::string& temp_path, std::string_view video_path, const blur_settings& settings);

	std::string get_filename() { return filename; }
};

inline c_avisynth_handler avisynth;