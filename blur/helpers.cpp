#include "includes.h"

std::string helpers::trim(std::string_view str) {
	str.remove_prefix(min(str.find_first_not_of(" \t\r\v\n"), str.size()));
	str.remove_suffix(min(str.size() - str.find_last_not_of(" \t\r\v\n") - 1, str.size()));

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
	std::wstring out(str.size() + 1, L'\0');

	int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &out[0], out.size());

	out.resize(size - 1);
	return out;
}

std::string helpers::to_lower(const std::string& str) {
	std::string out = str;

	std::for_each(out.begin(), out.end(), [](char & c){
		c = ::tolower(c);
	});

	return out;
}

void helpers::set_hidden(const std::string& path) {
	const char* path_str = path.c_str();
	int attr = GetFileAttributesA(path_str);
	SetFileAttributesA(path_str, attr | FILE_ATTRIBUTE_HIDDEN);
}

int helpers::exec(const std::string& command) {
	char buf[128];
	FILE* pipe;
	
	// run command and redirect output to pipe
	if ((pipe = _popen(std::string("\"" + command + "\"").c_str(), "rt")) == NULL)
		return -1;
	
	// read from pipe until end
	while (fgets(buf, 128, pipe)) {
		puts(buf);
	}
	
	// close pipe and return value
	if (feof(pipe))
		return _pclose(pipe);
	
	return -1;
}

bool helpers::detect_command(const std::string& command) {
	auto ret = exec("where /q " + command);
	return ret == 0;
}

std::string helpers::get_executable_path() {
	char buf[MAX_PATH];
	GetModuleFileNameA(NULL, buf, MAX_PATH);
	return std::string(buf);
}