#include "includes.h"

void c_config::create(const std::string& filepath, s_blur_settings current_settings) {
	std::ofstream output(filepath);

	output << "- blur" << "\n";
	output << "blur: " << (current_settings.blur ? "true" : "false") << "\n";
	output << "blur amount: " << current_settings.blur_amount << "\n";
	output << "blur output fps: " << current_settings.blur_output_fps << "\n";
	output << "blur weighting: " << current_settings.blur_weighting << "\n";
	
	output << "\n";
	output << "- interpolation" << "\n";
	output << "interpolate: " << (current_settings.interpolate ? "true" : "false") << "\n";
	output << "interpolated fps: " << current_settings.interpolated_fps << "\n";

	output << "\n";
	output << "- rendering" << "\n";
	output << "quality: " << current_settings.quality << "\n";
	output << "preview: " << (current_settings.preview ? "true" : "false") << "\n";
	output << "detailed filenames: " << (current_settings.detailed_filenames ? "true" : "false") << "\n";

	output << "\n";
	output << "- timescale" << "\n";
	output << "input timescale: " << current_settings.input_timescale << "\n";
	output << "output timescale: " << current_settings.output_timescale << "\n";
	output << "adjust timescaled audio pitch: " << (current_settings.output_timescale_audio_pitch ? "true" : "false") << "\n";

	output << "\n";
	output << "- filters" << "\n";
	output << "brightness: " << current_settings.brightness << "\n";
	output << "saturation: " << current_settings.saturation << "\n";
	output << "contrast: " << current_settings.contrast << "\n";

	output << "\n";
	output << "- advanced rendering" << "\n";
	output << "gpu: " << (current_settings.gpu ? "true" : "false") << "\n";
	output << "gpu type (nvidia/amd/intel): " << current_settings.gpu_type << "\n";
	output << "deduplicate: " << (current_settings.deduplicate ? "true" : "false") << "\n";
	output << "custom ffmpeg filters: " << current_settings.ffmpeg_override << "\n";
	
	output << "\n";
	output << "- advanced blur" << "\n";
	output << "blur weighting gaussian std dev: " << current_settings.blur_weighting_gaussian_std_dev << "\n";
	output << "blur weighting triangle reverse: " << (current_settings.blur_weighting_triangle_reverse ? "true" : "false") << "\n";
	output << "blur weighting bound: " << current_settings.blur_weighting_bound << "\n";

	output << "\n";
	output << "- advanced interpolation" << "\n";
	output << "interpolation program (svp/rife/rife-ncnn): " << current_settings.interpolation_program << "\n";
	output << "interpolation speed: " << current_settings.interpolation_speed << "\n";
	output << "interpolation tuning: " << current_settings.interpolation_tuning << "\n";
	output << "interpolation algorithm: " << current_settings.interpolation_algorithm;
}

std::string c_config::get_config_filename(const std::string& video_folder) {
	return video_folder + filename;
}

s_blur_settings c_config::parse(const std::string& config_filepath, bool& first_time) {
	auto read_config = [&]() {
		std::map<std::string, std::string> config = {};

		// check if the config file exists, if not, write the default values
		if (!std::filesystem::exists(config_filepath)) {
			first_time = true;
			create(config_filepath);
		}

		// retrieve all of the variables in the config file
		std::ifstream input(config_filepath);
		std::string line;
		while (std::getline(input, line)) {
			// get key & value
			auto pos = line.find(':');
			if (pos == std::string::npos) // not a variable
				continue;

			std::string key = line.substr(0, pos);
			std::string value = line.substr(pos + 1);

			// trim whitespace
			key = helpers::trim(key);
			if (key == "")
				continue;

			value = helpers::trim(value);

			if (key != "custom ffmpeg filters") {
				// remove all spaces in values (it breaks stringstream string parsing, this is a dumb workaround) todo: better solution
				std::string::iterator end_pos = std::remove(value.begin(), value.end(), ' ');
				value.erase(end_pos, value.end());
			}

			config[key] = value;
		}

		return config;
	};

	auto config = read_config();

	auto config_get = [&]<typename t>(const std::string& var, t& out) {
		if (!config.contains(var)) {
			console.print(fmt::format("config missing variable '{}', adding and setting default value", var));
			return;
		}

		try {
			std::stringstream ss(config[var]);
			ss.exceptions(std::ios::failbit); // enable exceptions
			ss >> std::boolalpha >> out; // boolalpha: enable true/false bool parsing
		}
		catch (const std::exception&) {
			throw std::exception(fmt::format("failed to parse config variable '{}' (value: {})", var, config[var]).c_str());
		}
	};

	auto config_get_str = [&](const std::string& var, std::string& out) { // todo: clean this up i cant be bothered rn
		if (!config.contains(var)) {
			console.print(fmt::format("config missing variable '{}', adding and setting default value", var));
			return;
		}

		out = config[var];
	};

	s_blur_settings settings;

	config_get("blur", settings.blur);
	config_get("blur amount", settings.blur_amount);
	config_get("blur output fps", settings.blur_output_fps);
	config_get_str("blur weighting", settings.blur_weighting);
	
	config_get("interpolate", settings.interpolate);
	config_get("interpolated fps", settings.interpolated_fps);

	config_get("brightness", settings.brightness);
	config_get("saturation", settings.saturation);
	config_get("contrast", settings.contrast);

	config_get("quality", settings.quality);
	config_get("preview", settings.preview);
	config_get("detailed filenames", settings.detailed_filenames);

	config_get("input timescale", settings.input_timescale);
	config_get("output timescale", settings.output_timescale);
	config_get("adjust timescaled audio pitch", settings.output_timescale_audio_pitch);
	
	config_get("gpu", settings.gpu);
	config_get_str("gpu type (nvidia/amd/intel)", settings.gpu_type);
	config_get("deduplicate", settings.deduplicate);
	config_get_str("custom ffmpeg filters", settings.ffmpeg_override);

	config_get("blur weighting gaussian std dev", settings.blur_weighting_gaussian_std_dev);
	config_get("blur weighting triangle reverse", settings.blur_weighting_triangle_reverse);
	config_get_str("blur weighting bound", settings.blur_weighting_bound);

	config_get_str("interpolation program (svp/rife/rife-ncnn)", settings.interpolation_program);
	config_get_str("interpolation speed", settings.interpolation_speed);
	config_get_str("interpolation tuning", settings.interpolation_tuning);
	config_get_str("interpolation algorithm", settings.interpolation_algorithm);
	
	// recreate the config file using the parsed values (keeps nice formatting)
	create(config_filepath, settings);

	return settings;
}

s_blur_settings c_config::parse_folder(const std::string& video_folder, std::string& config_filepath, bool& first_time) {
	config_filepath = get_config_filename(video_folder);
	return parse(config_filepath, first_time);
}