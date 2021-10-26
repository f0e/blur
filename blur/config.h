#pragma once

#include <string>

struct s_blur_settings {
	bool blur = true;
	float blur_amount = 1.f;
	int blur_output_fps = 60;
	std::string blur_weighting = "equal";

	bool interpolate = true;
	int interpolated_fps = 480;

	float input_timescale = 1.f;
	float output_timescale = 1.f;
	bool output_timescale_audio_pitch = false;

	float brightness = 1.f;
	float saturation = 1.f;
	float contrast = 1.f;

	int quality = 18;
	bool preview = true;
	bool detailed_filenames = false;

	bool gpu = false;
	std::string gpu_type = "nvidia";
	bool deduplicate = false;
	std::string ffmpeg_override = "";

	float blur_weighting_gaussian_std_dev = 2.f;
	bool blur_weighting_triangle_reverse = false;
	std::string blur_weighting_bound = "[0, 2]";

	std::string interpolation_program = "svp";
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