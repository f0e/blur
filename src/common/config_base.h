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

			config[key] = value;
		}

		return config;
	}

	template<typename T>
	void extract_config_value(
		const std::map<std::string, std::string>& config, const std::string& var, T& out
	) { // todo: this (i think) takes more time than necessary sometimes (happened when i imported a config that was
		// just one value)
		auto it = config.find(var);
		if (it == config.end()) {
			DEBUG_LOG("config missing variable '{}'", var);
			return;
		}

		const auto& raw_value = it->second;

		if constexpr (std::is_same_v<T, std::string>) {
			out = raw_value;
		}
		else {
			try {
				std::stringstream ss(raw_value);
				ss.exceptions(std::ios::failbit); // enable exceptions
				ss >> std::boolalpha >> out;      // boolalpha: enable true/false bool parsing
			}
			catch (const std::exception&) {
				DEBUG_LOG("failed to parse config variable '{}' (value: {})", var, config.at(var));
			}
		}
	}

	inline std::mutex config_file_mutex;

	inline std::optional<std::string> read_config_file(const std::filesystem::path& filepath) {
		std::lock_guard lock(config_file_mutex);

		std::ifstream file(filepath);
		if (!file)
			return {};

		return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	}

	inline bool write_config_string(const std::filesystem::path& filepath, const std::string& content) {
		std::lock_guard lock(config_file_mutex);

		// don't write if the content is the same
		{
			std::ifstream existing(filepath);
			if (existing) {
				std::string current((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
				if (current == content)
					return false;
			}
		}

		std::ofstream output(filepath);
		output << content;

		return true;
	}

	// config files get renamed from time to time. rename an old one into place as it's found, so an existing
	// install keeps its settings instead of silently starting over on defaults. does nothing if the old file is
	// gone, or if the new one is already there
	inline void migrate_file(const std::filesystem::path& from, const std::filesystem::path& to) {
		std::lock_guard lock(config_file_mutex);

		std::error_code ec; // a failed migration just leaves the old file where it is, which is recoverable
		if (!std::filesystem::exists(from, ec) || std::filesystem::exists(to, ec))
			return;

		std::filesystem::rename(from, to, ec);

		if (ec)
			u::log("failed to migrate config '{}' to '{}': {}", from, to, ec.message());
		else
			u::log("migrated config '{}' to '{}'", from, to);
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
