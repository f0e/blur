#pragma once

#include <string>

struct s_blur_settings {
	int cpu_cores = 4;

	int input_fps = 60;
	int output_fps = 60;

	float input_timescale = 1.f;
	float output_timescale = 1.f;

	bool blur = true;
	float blur_amount = 0.6f;

	bool interpolate = true;
	int interpolated_fps = 600;
	
	int crf = 18;

	bool preview = true;
	bool detailed_filenames = false;

	bool gpu = false;
	bool multithreading = true;

	std::string interpolation_speed = "default";
	std::string interpolation_tuning = "default";
	std::string interpolation_algorithm = "default";
};

class c_config {
private:
	const std::string filename = ".blur-config.cfg";

private:
	std::string get_config_filename(const std::string& video_folder);

public:
	void create(const std::string& filepath, s_blur_settings current_settings = s_blur_settings());

	s_blur_settings parse(const std::string& video_folder, std::string& config_filepath, bool& first_time);
};

inline c_config config;