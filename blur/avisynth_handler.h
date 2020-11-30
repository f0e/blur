#pragma once

#include "config.h"

class c_avisynth_handler {
private:
	const std::string avs_path = "blur_temp/";
	std::string filename;

private:
	// create avs file path
	void create_path();

	// remove avs file path
	void remove_path();

public:
	// create an avisynth file using input filename and settings
	void create(std::string_view video_path, const blur_settings& settings);

	// delete script
	void erase();

	std::string get_filename() { return filename; }
	std::string get_path() { return avs_path; }
};

extern c_avisynth_handler avisynth;