#pragma once

#include <string>

struct blur_settings {
	int cpu_cores = 4;
	int cpu_threads = 4;

	int input_fps = 60;
	int output_fps = 30;

	float timescale = 1.f;

	bool blur = true;
	float exposure = 0.5;

	bool interpolate = true;
	int interpolated_fps = 480;
};

class c_config {
private:
	std::string filename = "config.cfg";
	
public:
	// creates the config file with template values
	void create();
	
	// parses the config file, returns settings
	blur_settings parse();
};

extern c_config config;