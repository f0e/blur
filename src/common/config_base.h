#pragma once

namespace config_base {
	template<typename T>
	concept HasGetline = requires(T& t, std::string& s) { std::getline(t, s); };

	template<HasGetline InputStream>
	std::map<std::string, std::string> read_config_map(InputStream& input_stream) {
		std::map<std::string, std::string> config = {};

		// retrieve all of the variables from the input source
		std::string line;
		while (std::getline(input_stream, line)) {
			// get key & value
			auto pos = line.find(':');
			if (pos == std::string::npos) // not a variable
				continue;

			std::string key = line.substr(0, pos);
			std::string value = line.substr(pos + 1);

			// trim whitespace
			key = u::trim(key);
			if (key == "")
				continue;

			value = u::trim(value);

			if (key != "custom ffmpeg filters") {
				// remove all spaces in values (it breaks stringstream string parsing, this is a dumb workaround)
				// todo: better solution
				std::erase(value, ' ');
			}

			config[key] = value;
		}

		return config;
	}

	template<typename T>
	void extract_config_value(
		const std::map<std::string, std::string>& config, const std::string& var, T& out
	) { // todo: this (i think) takes more time than necessary sometimes (happened when i imported a config that was
		// just one value)
		if (!config.contains(var)) {
			DEBUG_LOG("config missing variable '{}'", var);
			return;
		}

		try {
			std::stringstream ss(config.at(var));
			ss.exceptions(std::ios::failbit); // enable exceptions
			ss >> std::boolalpha >> out;      // boolalpha: enable true/false bool parsing
		}
		catch (const std::exception&) {
			DEBUG_LOG("failed to parse config variable '{}' (value: {})", var, config.at(var));
			return;
		}
	}

	inline void extract_config_string(
		const std::map<std::string, std::string>& config, const std::string& var, std::string& out
	) {
		if (!config.contains(var)) {
			DEBUG_LOG("config missing variable '{}'", var);
			return;
		}

		out = config.at(var);
	}

	template<typename ConfigType>
	ConfigType load_config(
		const std::filesystem::path& config_path,
		void (*create_func)(const std::filesystem::path&, const ConfigType&),
		ConfigType (*parse_func)(const std::filesystem::path&)
	) {
		bool config_exists = std::filesystem::exists(config_path);

		if (!config_exists) {
			create_func(config_path, ConfigType());

			if (blur.verbose)
				u::log("Configuration file not found, default config generated at {}", config_path);
		}

		return parse_func(config_path);
	}
}
