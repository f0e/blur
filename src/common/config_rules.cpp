#include "config_rules.h"
#include "config_base.h"

std::string config_rules::generate_config_string(const ConfigRuleSettings& settings) {
	std::ostringstream output;

	output << "[blur v" << BLUR_VERSION << "]" << "\n";

	output << "\n";
	output << "default config: " << settings.default_config << "\n";

	for (const auto& rule : settings.rules) {
		output << "\n";
		output << "- rule" << "\n";
		output << "config: " << rule.config_name << "\n";
		output << "pattern: " << rule.pattern << "\n";
	}

	return output.str();
}

void config_rules::create(const std::filesystem::path& filepath, const ConfigRuleSettings& current_settings) {
	config_base::write_config_string(filepath, generate_config_string(current_settings));
}

ConfigRuleSettings config_rules::parse(const std::string& config_content) {
	ConfigRuleSettings settings;

	std::istringstream stream(config_content);
	std::string line;

	bool in_rule = false;

	while (std::getline(stream, line)) {
		line = u::trim(line);

		if (line.empty() || line.front() == '[' || line.front() == '#')
			continue;

		if (line.front() == '-') {
			in_rule = u::trim(line.substr(1)) == "rule";

			if (in_rule)
				settings.rules.emplace_back();

			continue;
		}

		size_t delimiter_pos = line.find(':');
		if (delimiter_pos == std::string::npos)
			continue;

		// split on the first ':' so a pattern is free to contain one, as 'D:/clips/*' does. config
		// names can't, they go through u::validate_filename
		std::string key = u::trim(line.substr(0, delimiter_pos));
		std::string value = u::trim(line.substr(delimiter_pos + 1));

		if (!in_rule) {
			if (key == "default config")
				settings.default_config = value;

			continue;
		}

		auto& rule = settings.rules.back();

		if (key == "config")
			rule.config_name = value;
		else if (key == "pattern")
			rule.pattern = value;
	}

	return settings;
}

ConfigRuleSettings config_rules::parse(const std::filesystem::path& config_filepath) {
	auto content = config_base::read_config_file(config_filepath);
	if (!content)
		return DEFAULT_CONFIG;

	return parse(*content);
}

std::filesystem::path config_rules::get_config_path() {
	return blur.settings_path / CONFIG_FILENAME;
}

ConfigRuleSettings config_rules::get_config() {
	return config_base::load_config<ConfigRuleSettings>(get_config_path(), create, parse);
}

void config_rules::save(const ConfigRuleSettings& settings) {
	create(get_config_path(), settings);
}

bool config_rules::usable(const ConfigRule& rule, const std::vector<std::string>& available_configs) {
	if (rule.pattern.empty() || rule.config_name.empty())
		return false;

	// a rule outliving the config it points at is kept rather than deleted, so skip it here instead
	// of rendering with something the user didn't ask for
	return u::contains(available_configs, rule.config_name);
}

bool config_rules::any_usable(const ConfigRuleSettings& settings, const std::vector<std::string>& available_configs) {
	return std::ranges::any_of(settings.rules, [&](const ConfigRule& rule) {
		return usable(rule, available_configs);
	});
}

const ConfigRule* config_rules::find_match(
	const ConfigRuleSettings& settings,
	const std::filesystem::path& input_path,
	const std::vector<std::string>& available_configs
) {
	auto path_string = u::path_to_string(input_path);

	for (const auto& rule : settings.rules) {
		if (!usable(rule, available_configs))
			continue;

		if (u::matches_pattern(rule.pattern, path_string))
			return &rule;
	}

	return nullptr;
}

std::string config_rules::match(
	const ConfigRuleSettings& settings,
	const std::filesystem::path& input_path,
	const std::vector<std::string>& available_configs
) {
	const auto* rule = find_match(settings, input_path, available_configs);

	return rule ? rule->config_name : std::string{};
}

void config_rules::rename_config(ConfigRuleSettings& settings, const std::string& from, const std::string& to) {
	for (auto& rule : settings.rules) {
		if (rule.config_name == from)
			rule.config_name = to;
	}

	if (settings.default_config == from)
		settings.default_config = to;
}
