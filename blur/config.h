#pragma once

#include <string>

struct blur_settings {
	int cpu_cores = 4;
	int cpu_threads = 4;

	int input_fps = 60;
	int output_fps = 30;

	float timescale = 1.f;

	bool blur = true;
	float blur_amount = 0.5;

	bool interpolate = true;
	int interpolated_fps = 480;

	std::string interpolation_speed = "default";
	std::string interpolation_tuning = "default";
	std::string interpolation_algorithm = "default";
	
	int crf = 18;

	bool preview = true;
};

class c_config {
private:
	std::string filename = ".blur-config.cfg";
	
public:
	// creates the config file with template values
	void create(blur_settings current_settings = blur_settings());

	// parses the config file, returns settings
	blur_settings parse();
};

extern c_config config;