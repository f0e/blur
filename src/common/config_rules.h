#pragma once

#include "config_blur.h" // for DEFAULT_CONFIG_NAME

// a rule points every video whose path matches `pattern` at `config_name`. rules are globally
// ordered and the first match wins, so overlapping patterns resolve predictably
struct ConfigRule {
	std::string pattern;
	std::string config_name;

	bool operator==(const ConfigRule& other) const = default;
};

struct ConfigRuleSettings {
	std::vector<ConfigRule> rules;

	// name-based so renames preserve the default and empty requires a queue selection
	std::string default_config = std::string(config_blur::DEFAULT_CONFIG_NAME);

	bool operator==(const ConfigRuleSettings& other) const = default;
};

namespace config_rules {
	inline const ConfigRuleSettings DEFAULT_CONFIG;

	const std::string CONFIG_FILENAME = "rules.cfg";

	std::string generate_config_string(const ConfigRuleSettings& settings);

	void create(const std::filesystem::path& filepath, const ConfigRuleSettings& current_settings = {});

	ConfigRuleSettings parse(const std::string& config_content);
	ConfigRuleSettings parse(const std::filesystem::path& config_filepath);

	std::filesystem::path get_config_path();

	ConfigRuleSettings get_config();

	void save(const ConfigRuleSettings& settings);

	// available_configs is passed in rather than read from config_blur, which keeps the matching
	// pure and keeps this out of a cycle with config_blur
	bool usable(const ConfigRule& rule, const std::vector<std::string>& available_configs);
	bool any_usable(const ConfigRuleSettings& settings, const std::vector<std::string>& available_configs);

	// the first usable rule matching input_path, or null if nothing matches
	const ConfigRule* find_match(
		const ConfigRuleSettings& settings,
		const std::filesystem::path& input_path,
		const std::vector<std::string>& available_configs
	);

	// the config the first matching usable rule picks, or empty if nothing matches
	std::string match(
		const ConfigRuleSettings& settings,
		const std::filesystem::path& input_path,
		const std::vector<std::string>& available_configs
	);

	void rename_config(ConfigRuleSettings& settings, const std::string& from, const std::string& to);
}
