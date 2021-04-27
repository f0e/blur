#include "config.h"

#include "includes.h"

c_config config;

void c_config::create(std::string_view filepath, blur_settings current_settings) {
	std::ofstream output(filepath);

	output << "cpu cores: " << current_settings.cpu_cores << "\n";
	output << "cpu threads: " << current_settings.cpu_threads << "\n";

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
	
	output << "\n";
	output << "crf: " << current_settings.crf << "\n";
	output << "gpu: " << (current_settings.gpu ? "true" : "false") << "\n";
	output << "detailed filenames: " << (current_settings.detailed_filenames ? "true" : "false") << "\n";
	
	output << "\n";

	output << "interpolation speed: " << current_settings.interpolation_speed << "\n";
	output << "interpolation tuning: " << current_settings.interpolation_tuning << "\n";
	output << "interpolation algorithm: " << current_settings.interpolation_algorithm << "\n";
}

std::string_view trim(std::string_view s) {
	s.remove_prefix(min(s.find_first_not_of(" \t\r\v\n"), s.size()));
	s.remove_suffix(min(s.size() - s.find_last_not_of(" \t\r\v\n") - 1, s.size()));

	return s;
}

bool config_get(std::map<std::string, std::string>& config, const std::string& var, std::string& out) {
	if (!config.contains(var))
		return false;

	out = config[var];
	return true;
	return true;
}

bool config_get(std::map<std::string, std::string>& config, const std::string& var, int& out) {
	std::string str_out;
	if (!config_get(config, var, str_out))
		return false;

	out = std::stoi(str_out);
	return true;
}

bool config_get(std::map<std::string, std::string>& config, const std::string& var, float& out) {
	std::string str_out;
	if (!config_get(config, var, str_out))
		return false;

	out = std::stof(str_out);
	return true;
}

bool config_get(std::map<std::string, std::string>& config, const std::string& var, bool& out) {
	std::string str_out;
	if (!config_get(config, var, str_out))
		return false;

	out = str_out == "true" ? true : false;
	return true;
}

std::string c_config::get_config_filename(const std::string& video_folder) {
	return video_folder + filename;
}

blur_settings c_config::parse(const std::string& video_folder, bool& first_time, std::string& config_filepath) {
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
			key = trim(key);
			value = trim(value);

			if (key == "" || value == "")
				continue;

			config[key] = value;
		}

		return config;
	};

	try {
		auto config = read_config();

		blur_settings settings;

		bool ok = true;

		if (!config_get(config, "cpu cores", settings.cpu_cores)) ok = false;
		if (!config_get(config, "cpu threads", settings.cpu_threads)) ok = false;

		if (!config_get(config, "input fps", settings.input_fps)) ok = false;
		if (!config_get(config, "output fps", settings.output_fps)) ok = false;

		if (!config_get(config, "input timescale", settings.input_timescale)) ok = false;
		if (!config_get(config, "output timescale", settings.output_timescale)) ok = false;

		if (!config_get(config, "blur", settings.blur)) ok = false;
		if (!config_get(config, "blur amount", settings.blur_amount)) ok = false;

		if (!config_get(config, "interpolate", settings.interpolate)) ok = false;
		if (!config_get(config, "interpolated fps", settings.interpolated_fps)) ok = false;

		if (!config_get(config, "interpolation speed", settings.interpolation_speed)) ok = false;
		if (!config_get(config, "interpolation tuning", settings.interpolation_tuning)) ok = false;
		if (!config_get(config, "interpolation algorithm", settings.interpolation_algorithm)) ok = false;

		if (!config_get(config, "crf", settings.crf)) ok = false;

		if (!config_get(config, "preview", settings.preview)) ok = false;
		if (!config_get(config, "gpu", settings.gpu)) ok = false;
		if (!config_get(config, "detailed filenames", settings.detailed_filenames)) ok = false;
		
		if (!ok) {
			// one or more variables weren't loaded, so recreate the config file (including currently loaded settings)
			create(config_filepath, settings);
		}

		//	// convert strings to lowercase
		//	std::transform(settings.interpolation_speed.begin(), settings.interpolation_speed.end(), settings.interpolation_speed.begin(), [](unsigned char c) { return std::tolower(c); });
		//	std::transform(settings.interpolation_tuning.begin(), settings.interpolation_tuning.end(), settings.interpolation_tuning.begin(), [](unsigned char c) { return std::tolower(c); });

		return settings;
	}
	catch (std::exception e) {
		throw std::exception("failed to parse config, try deleting");
	}
}