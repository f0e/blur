#include "includes.h"

void c_config::create(const std::string& filepath, s_blur_settings current_settings) {
	std::ofstream output(filepath);

	output << "cpu cores: " << current_settings.cpu_cores << "\n";

	output << "\n";

	output << "input fps: " << current_settings.input_fps << "\n";
	output << "output fps: " << current_settings.output_fps << "\n";

	output << "\n";

	output << "input timescale: " << current_settings.input_timescale << "\n";
	output << "output timescale: " << current_settings.output_timescale << "\n";

	output << "\n";

	output << "blur: " << (current_settings.blur ? "true" : "false") << "\n";
	output << "blur amount: " << current_settings.blur_amount << "\n";

	output << "\n";

	output << "interpolate: " << (current_settings.interpolate ? "true" : "false") << "\n";
	output << "interpolated fps: " << current_settings.interpolated_fps << "\n";

	output << "\n";

	output << "preview: " << (current_settings.preview ? "true" : "false") << "\n";
	output << "detailed filenames: " << (current_settings.detailed_filenames ? "true" : "false") << "\n";

	output << "\n";
	output << "crf: " << current_settings.crf << "\n";
	output << "gpu: " << (current_settings.gpu ? "true" : "false") << "\n";
	output << "multithreading: " << (current_settings.multithreading ? "true" : "false") << "\n";

	output << "\n";

	output << "interpolation speed: " << current_settings.interpolation_speed << "\n";
	output << "interpolation tuning: " << current_settings.interpolation_tuning << "\n";
	output << "interpolation algorithm: " << current_settings.interpolation_algorithm << "\n";
}

std::string c_config::get_config_filename(const std::string& video_folder) {
	return video_folder + filename;
}

s_blur_settings c_config::parse(const std::string& video_folder, std::string& config_filepath, bool& first_time) {
	config_filepath = get_config_filename(video_folder);

	auto read_config = [&]() {
		std::map<std::string, std::string> config = {};

		// check if the config file exists, if not, write the default values
		if (!std::filesystem::exists(config_filepath)) {
			first_time = true;
			create(config_filepath);
		}

		// retrieve all of the variables in the config file
		std::ifstream input(config_filepath);
		while (input) {
			std::string key, value;

			std::getline(input, key, ':');
			std::getline(input, value, '\n');

			// trim whitespace
			key = helpers::trim(key);
			value = helpers::trim(value);

			if (key == "" || value == "")
				continue;

			config[key] = value;
		}

		return config;
	};

	auto config = read_config();

	bool missing_a_variable = false;

	auto config_get = [&]<typename t>(const std::string& var, t& out) {
		if (!config.contains(var)) {
			missing_a_variable = true;
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

	s_blur_settings settings;

	config_get("cpu cores", settings.cpu_cores);

	config_get("input fps", settings.input_fps);
	config_get("output fps", settings.output_fps);

	config_get("input timescale", settings.input_timescale);
	config_get("output timescale", settings.output_timescale);

	config_get("blur", settings.blur);
	config_get("blur amount", settings.blur_amount);

	config_get("interpolate", settings.interpolate);
	config_get("interpolated fps", settings.interpolated_fps);

	config_get("crf", settings.crf);

	config_get("preview", settings.preview);
	config_get("detailed filenames", settings.detailed_filenames);

	config_get("gpu", settings.gpu);
	config_get("multithreading", settings.multithreading);

	config_get("interpolation speed", settings.interpolation_speed);
	config_get("interpolation tuning", settings.interpolation_tuning);
	config_get("interpolation algorithm", settings.interpolation_algorithm);

	if (missing_a_variable) {
		// one or more variables weren't loaded, so recreate the config file (including currently loaded settings)
		create(config_filepath, settings);
	}

	return settings;
}