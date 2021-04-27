#pragma once

#include <string>

struct blur_settings {
	int cpu_cores = 4;
	int cpu_threads = 4;

	int input_fps = 60;
	int output_fps = 60;

	float input_timescale = 1.f;
	float output_timescale = 1.f;

	bool blur = true;
	float blur_amount = 0.6f;

	bool interpolate = true;
	int interpolated_fps = 600;

	std::string interpolation_speed = "default";
	std::string interpolation_tuning = "default";
	std::string interpolation_algorithm = "default";
	
	int crf = 18;

	bool preview = true;
	bool gpu = false;
	bool detailed_filenames = false;
};

class c_config {
private:
	std::string filename = ".blur-config.cfg";
	
public:
	void create(std::string_view filepath, blur_settings current_settings = blur_settings());

	std::string get_config_filename(const std::string& video_folder);
	blur_settings parse(const std::string& video_folder, bool& first_time, std::string& config_filepath);
};

extern c_config config;