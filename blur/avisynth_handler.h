#pragma once

#include "config.h"

class c_avisynth_handler {
private:
	const std::string path = "avs/";
	std::string filename;

private:
	// create avs file path
	void create_path();

	// remove avs file path
	void remove_path();
public:
	// create an avisynth file using input filename and settings
	void create(std::string input_file, blur_settings& settings);

	// get script filename
	std::string get_filename() { return filename; }

	// delete script
	void erase();
};

extern c_avisynth_handler avisynth;