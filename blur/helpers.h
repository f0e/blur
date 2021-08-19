#pragma once

namespace helpers {
	std::string trim(std::string_view str);
	std::string random_string(int len);
	std::vector<std::string> split_string(std::string str, const std::string& delimiter);
	std::wstring towstring(const std::string& str);
	std::string to_lower(const std::string& str);

	void set_hidden(const std::string& path);

	int exec(const std::string& command);
	bool detect_command(const std::string& command);

	std::string get_executable_path();

	template <typename TP>
	inline std::time_t to_time_t(TP tp) {
		using namespace std::chrono;
		auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now()
			+ system_clock::now());
		return system_clock::to_time_t(sctp);
	}
}