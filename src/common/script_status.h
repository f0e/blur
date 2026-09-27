#pragma once

// see blur/log.py

namespace script_status {
	struct Status {
		std::string key;
		std::string value;
	};

	std::optional<Status> parse(std::string_view line);

	std::map<std::string, std::string> read_all(std::istream& stream);
}
