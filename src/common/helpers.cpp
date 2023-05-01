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
	int length = MultiByteToWideChar(CP_UTF8, 0, str.data(), str.size(), NULL, 0);
	std::wstring ret(length, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.data(), str.size(), &ret[0], length);
	return ret;
}

std::string helpers::tostring(const std::wstring& wstr) {
	int length = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wstr.size(), nullptr, 0, nullptr, nullptr);
	std::string ret(length, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wstr.size(), &ret[0], length, nullptr, nullptr);
	return ret;
}

std::string helpers::to_lower(const std::string& str) {
	std::string out = str;

	std::for_each(out.begin(), out.end(), [](char& c) {
		c = ::tolower(c);
		});

	return out;
}

void helpers::set_hidden(const std::filesystem::path& path) {
	auto path_str = path.wstring().c_str();
	int attr = GetFileAttributes(path_str);
	SetFileAttributes(path_str, attr | FILE_ATTRIBUTE_HIDDEN);
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
	char buf[MAX_PATH];
	GetModuleFileNameA(NULL, buf, MAX_PATH);
	return std::string(buf);
}

void helpers::read_line_from_handle(HANDLE handle, std::function<void(std::string)> on_line_fn) {
	constexpr size_t BUFFER_SIZE = 4096;
	std::string output;
	std::array<char, BUFFER_SIZE> buffer{};

	DWORD bytesRead;
	while (ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead != 0) {
		output.append(buffer.data(), bytesRead);

		debug_log("output: %s\n", output.c_str());

		size_t pos = 0;
		while ((pos = output.find('\r')) != std::string::npos) {
			std::string line = output.substr(0, pos);

			on_line_fn(line);

			output.erase(0, pos + 1); // Remove the extracted line from the output
		}
	}

	if (!output.empty()) {
		debug_log("leftover... %s\n", output.c_str());
	}
};

struct ProcHwnd {
	DWORD proc_id;
	HWND  hwnd;
};
BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM lParam) {
	DWORD processId;
	GetWindowThreadProcessId(hwnd, &processId);
	if (processId == ((ProcHwnd*)lParam)->proc_id) {
		((ProcHwnd*)lParam)->hwnd = hwnd;
		return FALSE;
	}
	return TRUE;
}

HWND helpers::get_window(DWORD dwProcessId) {
	// Once the program has started you can look for the window.
	ProcHwnd ph{ dwProcessId, 0 };
	EnumWindows(enum_windows_proc, reinterpret_cast<LPARAM>(&ph));
	return ph.hwnd;
}