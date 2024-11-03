#include "helpers.h"

std::string helpers::trim(std::string_view str) {
	str.remove_prefix(std::min(str.find_first_not_of(" \t\r\v\n"), str.size()));
	str.remove_suffix(std::min(str.size() - str.find_last_not_of(" \t\r\v\n") - 1, str.size()));

	return str.data();
}

std::string helpers::random_string(int len) {
	std::string str("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");

	std::random_device rd;
	std::mt19937 generator(rd());

	std::shuffle(str.begin(), str.end(), generator);

	return str.substr(0, len);
}

std::vector<std::string> helpers::split_string(std::string str, const std::string& delimiter) {
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

std::wstring helpers::towstring(const std::string& str) {
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	return converter.from_bytes(str);
}

std::string helpers::tostring(const std::wstring& wstr) {
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	return converter.to_bytes(wstr);
}

std::string helpers::to_lower(const std::string& str) {
	std::string out = str;

	std::for_each(out.begin(), out.end(), [](char& c) {
		c = ::tolower(c);
	});

	return out;
}

int helpers::exec(std::wstring command, std::wstring run_dir) {
	return 0;
}

bool helpers::detect_command(const std::string& command) {
	// todo
	return true;
	// auto ret = exec("where /q " + command);
	// return ret == 0;
}

std::string helpers::get_executable_path() {
#if defined(_WIN32)
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	return std::string(path);
#elif defined(__linux__)
	char path[PATH_MAX];
	ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
	return std::string(path, (count > 0) ? count : 0);
#elif defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size); // Get the required size
	std::vector<char> path(size);
	if (_NSGetExecutablePath(path.data(), &size) == 0) {
		return std::string(path.data());
	}
	return "";
#else
#	error "Unsupported platform"
#endif
}
