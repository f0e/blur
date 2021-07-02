#pragma once

namespace helpers {
	inline std::string_view trim(std::string_view s) {
		s.remove_prefix(min(s.find_first_not_of(" \t\r\v\n"), s.size()));
		s.remove_suffix(min(s.size() - s.find_last_not_of(" \t\r\v\n") - 1, s.size()));

		return s;
	}

	inline std::vector<std::string> split_string(std::string str, const std::string& delimiter) {
		std::vector<std::string> output;

		size_t pos = 0;
		while ((pos = str.find(delimiter)) != std::string::npos) {
			std::string token = str.substr(0, pos);
			output.push_back(token);
			str.erase(0, pos + delimiter.length());
		}

		output.push_back(str);

		return output;
	}

	template <typename TP>
	inline std::time_t to_time_t(TP tp) {
		using namespace std::chrono;
		auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now()
			+ system_clock::now());
		return system_clock::to_time_t(sctp);
	}
}