#pragma once

#include "updates.h"

namespace config_base {
	using ConfigMap = std::map<std::string, std::string>;

	template<typename T>
	concept HasGetline = requires(T& t, std::string& s) { std::getline(t, s); };

	template<HasGetline InputStream>
	ConfigMap read_config_map(InputStream& input_stream) {
		ConfigMap config = {};

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

	inline std::optional<std::string> parse_config_version(const std::string& config_content) {
		std::istringstream stream(config_content);

		std::string line;
		while (std::getline(stream, line)) {
			line = u::trim(line);
			if (line.empty())
				continue;

			if (!line.starts_with("[blur v") || !line.ends_with("]"))
				return {}; // the header is always the first line

			return line.substr(7, line.size() - 8);
		}

		return {};
	}

	struct Migration {
		std::string_view version;
		std::string_view description;
		bool (*apply)(ConfigMap& config);
	};

	inline void apply_migrations(
		ConfigMap& config, const std::optional<std::string>& config_version, std::span<const Migration> migrations
	) {
		for (const auto& migration : migrations) {
			// unversioned configs could be from anything, so run every migration. ones from the migration's own version
			// can be from a dev build before it was added, and migrations only act on the old settings, so those run
			// too
			bool needed = !config_version || !updates::is_version_newer(migration.version, *config_version);
			if (!needed)
				continue;

			if (!migration.apply(config))
				continue;

			DEBUG_LOG(
				"migrated config from v{} for v{}: {}",
				config_version.value_or("?"),
				migration.version,
				migration.description
			);
		}
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
			// a failed read writes 0, so it goes through a copy to keep the default
			T value{};
			std::stringstream ss(raw_value);
			if (ss >> std::boolalpha >> value)
				out = value;
			else
				DEBUG_LOG("failed to parse config variable '{}' (value: {})", var, raw_value);
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

	// remove obsolete config when its replacement exists
	inline void migrate_file(const std::filesystem::path& from, const std::filesystem::path& to) {
		std::lock_guard lock(config_file_mutex);

		std::error_code ec;
		if (!std::filesystem::exists(from, ec))
			return;

		if (std::filesystem::exists(to, ec)) {
			std::filesystem::remove(from, ec);

			if (ec)
				u::log("failed to remove migrated config '{}': {}", from, ec.message());
			else
				u::log("removed migrated config '{}'", from);
			return;
		}

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
