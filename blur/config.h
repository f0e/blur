#pragma once

#include <string>

struct s_blur_settings {
	float input_timescale = 1.f;
	float output_timescale = 1.f;

	bool blur = true;
	float blur_amount = 0.6f;
	int blur_output_fps = 60;

	bool interpolate = true;
	int interpolated_fps = 600;
	
	int quality = 18;

	bool preview = true;
	bool detailed_filenames = false;

	bool multithreading = true;
	bool gpu = false;
	std::string gpu_type = "nvidia";

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

	s_blur_settings parse(const std::string& config_filepath, bool& first_time);
	s_blur_settings parse_folder(const std::string& video_folder, std::string& config_filepath, bool& first_time);
};

inline c_config config;