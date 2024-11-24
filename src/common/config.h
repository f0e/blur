#pragma once

struct s_blur_settings {
	bool blur = true;
	float blur_amount = 1.f;
	int blur_output_fps = 60;
	std::string blur_weighting = "equal";

	bool interpolate = true;
	std::string interpolated_fps = "5x";

	float input_timescale = 1.f;
	float output_timescale = 1.f;
	bool output_timescale_audio_pitch = false;

	float brightness = 1.f;
	float saturation = 1.f;
	float contrast = 1.f;

	int quality = 18;
	bool deduplicate = true;
	bool preview = true;
	bool detailed_filenames = false;

	bool gpu_interpolation = true;
	bool gpu_rendering = false;
	std::string gpu_type = "nvidia";
	std::string video_container = "mp4";
	std::string ffmpeg_override = "";
	bool debug = false;

	float blur_weighting_gaussian_std_dev = 2.f;
	bool blur_weighting_triangle_reverse = false;
	std::string blur_weighting_bound = "[0, 2]";

	std::string interpolation_program = "svp";
	std::string interpolation_preset = "weak";
	int interpolation_algorithm = 13;
	int interpolation_blocksize = 8;
	int interpolation_mask_area = 0;

	bool manual_svp = false;
	std::string super_string = "";
	std::string vectors_string = "";
	std::string smooth_string = "";

public:
	nlohmann::json to_json();
};

const s_blur_settings default_settings;

class c_config {
private:
	const std::string filename = ".blur-config.cfg"; // todo: the . makes configs hidden on mac - is that an issue

public:
	void create(const std::filesystem::path& filepath, s_blur_settings current_settings = s_blur_settings());

	std::filesystem::path get_config_filename(const std::filesystem::path& video_folder);

	s_blur_settings parse(const std::filesystem::path& config_filepath);

	s_blur_settings get_config(const std::filesystem::path& config_filepath, bool use_global);
};

inline c_config config;
