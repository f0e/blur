#include "config.h"

#include "includes.h"

c_config config;

void c_config::create() {
	blur_settings template_settings;

	std::ofstream output(filename);

	output << "cpu_cores: " << template_settings.cpu_cores << "\n";
	output << "cpu_threads: " << template_settings.cpu_threads << "\n";

	output << "\n";

	output << "input_fps: " << template_settings.input_fps << "\n";
	output << "output_fps: " << template_settings.output_fps << "\n";

	output << "\n";

	output << "timescale: " << template_settings.timescale << "\n";

	output << "\n";

	output << "blur: " << (template_settings.blur ? "true" : "false") << "\n";
	output << "exposure: " << template_settings.exposure << "\n";

	output << "\n";

	output << "interpolate: " << (template_settings.interpolate ? "true" : "false") << "\n";
	output << "interpolated fps: " << template_settings.interpolated_fps << "\n";
}

std::string_view trim(std::string_view s) {
	s.remove_prefix(std::min(s.find_first_not_of(" \t\r\v\n"), s.size()));
	s.remove_suffix(std::min(s.size() - s.find_last_not_of(" \t\r\v\n") - 1, s.size()));

	return s;
}

blur_settings c_config::parse() {
	auto read_config = [&]() {
		std::map<std::string, std::string> config = {};

		// check if the config file exists, if not, write the default values
		if (!std::filesystem::exists(filename))
			create();

		// retrieve all of the variables in the config file
		std::ifstream input(filename);
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

		settings.cpu_cores = std::stoi(config["cpu_cores"]);
		settings.cpu_threads = std::stoi(config["cpu_threads"]);

		settings.input_fps = std::stoi(config["input_fps"]);
		settings.output_fps = std::stoi(config["output_fps"]);

		settings.timescale = std::stof(config["timescale"]);

		settings.blur = config["blur"] == "true" ? true : false;
		settings.exposure = std::stof(config["exposure"]);

		settings.interpolate = config["interpolate"] == "true" ? true : false;
		settings.interpolated_fps = std::stoi(config["interpolated fps"]);

		return settings;
	}
	catch (std::exception e) {
		throw std::exception("failed to parse config");
	}
}