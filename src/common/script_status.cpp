#include "script_status.h"

std::optional<script_status::Status> script_status::parse(std::string_view line) {
	constexpr std::string_view prefix = "[blur:status] ";

	auto at = line.find(prefix);
	if (at == std::string_view::npos)
		return {};

	auto status = line.substr(at + prefix.size());

	auto equals = status.find('=');
	if (equals == std::string_view::npos)
		return {};

	return Status{
		.key = std::string(status.substr(0, equals)),
		.value = u::trim(status.substr(equals + 1)),
	};
}

std::map<std::string, std::string> script_status::read_all(std::istream& stream) {
	std::map<std::string, std::string> statuses;

	std::string line;
	while (std::getline(stream, line)) {
		if (auto status = parse(line))
			statuses[status->key] = status->value;
	}

	return statuses;
}
