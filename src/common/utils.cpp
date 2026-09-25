#include "utils.h"

// NOLINTBEGIN gpt ass code
std::wstring u::towstring(const std::string& str) {
	if (str.empty())
		return std::wstring();

#ifdef _WIN32
	// Windows-specific implementation
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
	std::wstring result(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], size_needed);
	return result;
#else
	// POSIX systems (Linux, macOS, etc.)
	std::vector<wchar_t> buf(str.size() + 1);
	std::mbstowcs(&buf[0], str.c_str(), str.size() + 1);
	return std::wstring(&buf[0]);
#endif
}

std::string u::tostring(const std::wstring& wstr) {
	if (wstr.empty()) {
		return std::string();
	}

#ifdef _WIN32
	// Windows-specific implementation
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
	std::string result(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size_needed, nullptr, nullptr);
	return result;
#else
	// POSIX systems (Linux, macOS, etc.)
	std::vector<char> buf((wstr.size() + 1) * MB_CUR_MAX);
	size_t converted = std::wcstombs(&buf[0], wstr.c_str(), buf.size());
	if (converted == static_cast<size_t>(-1)) {
		return std::string(); // Conversion failed
	}
	return std::string(&buf[0], converted);
#endif
}

// NOLINTEND

std::string u::trim(std::string_view str) {
	str.remove_prefix(std::min(str.find_first_not_of(" \t\r\v\n"), str.size()));
	str.remove_suffix(std::min(str.size() - str.find_last_not_of(" \t\r\v\n") - 1, str.size()));

	return std::string(str);
}

std::string u::random_string(int len) {
	std::string str("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");

	std::random_device rd;
	std::mt19937 generator(rd());

	std::shuffle(str.begin(), str.end(), generator);

	return str.substr(0, len);
}

std::vector<std::string> u::split_string(std::string str, const std::string& delimiter) {
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

std::string u::to_lower(const std::string& str) {
	std::string out = str;

	std::ranges::for_each(out, [](char& c) {
		c = std::tolower(c);
	});

	return out;
}

std::string u::truncate_with_ellipsis(const std::string& input, std::size_t max_length) {
	const std::string ellipsis = "...";
	if (input.length() > max_length) {
		if (max_length <= ellipsis.length()) {
			return ellipsis.substr(0, max_length); // handle very small max_length
		}
		return input.substr(0, max_length - ellipsis.length()) + ellipsis;
	}
	return input;
}

namespace {
	std::string normalise_for_match(std::string_view str) {
		std::string out(str);

		std::ranges::for_each(out, [](char& c) {
			c = c == '\\' ? '/' : (char)std::tolower(c);
		});

		return out;
	}
}

bool u::matches_pattern(std::string_view pattern, std::string_view text) {
	if (pattern.empty())
		return false;

	std::string pat = normalise_for_match(pattern);
	std::string str = normalise_for_match(text);

	if (pat.find_first_of("*?") == std::string::npos)
		return str.find(pat) != std::string::npos;

	size_t p = 0;
	size_t s = 0;
	size_t star = std::string::npos;
	size_t star_s = 0;

	while (s < str.size()) {
		if (p < pat.size() && (pat[p] == '?' || pat[p] == str[s])) {
			p++;
			s++;
		}
		else if (p < pat.size() && pat[p] == '*') {
			star = p++;
			star_s = s;
		}
		else if (star != std::string::npos) {
			// let the last '*' match one more character and try again
			p = star + 1;
			s = ++star_s;
		}
		else
			return false;
	}

	while (p < pat.size() && pat[p] == '*') {
		p++;
	}

	return p == pat.size();
}

tl::expected<std::string, std::string> u::validate_filename(const std::string& entered_name) {
	std::string name = u::trim(entered_name);
	if (name.empty())
		return tl::unexpected("Enter a name.");

	// windows rules on every platform so files can be copied between machines
	if (name == "." || name == ".." || name.ends_with(' ') || name.ends_with('.') ||
	    name.find_first_of("<>:\"/\\|?*") != std::string::npos ||
	    std::ranges::any_of(name, [](unsigned char character) {
			return character < 32;
		}))
	{
		return tl::unexpected("That name contains characters that cannot be used in a filename.");
	}

	return name;
}

std::optional<std::filesystem::path> u::get_program_path(const std::string& program_name) {
	namespace bp = boost::process;
	namespace fs = boost::filesystem;

	fs::path program_path = bp::search_path(program_name);

	std::filesystem::path path(program_path.native());

	if (!std::filesystem::exists(path))
		return {};

	return path;
}

// NOLINTBEGIN gpt ass code
std::string u::get_executable_path() {
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

// NOLINTEND

constexpr int64_t PERIOD = 1;
constexpr int64_t TOLERANCE = 1'020'000;
constexpr int64_t MAX_TICKS = PERIOD * 9'500;

void u::sleep(double seconds) {
#ifndef WIN32
	std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
#else // KILLLLL WINDOWS
	using namespace std;
	using namespace chrono;

	auto t = high_resolution_clock::now();
	auto target = t + nanoseconds(int64_t(seconds * 1e9));

	static HANDLE timer;
	if (!timer)
		timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

	int64_t maxTicks = PERIOD * 9'500;
	for (;;) {
		int64_t remaining = (target - t).count();
		int64_t ticks = (remaining - TOLERANCE) / 100;
		if (ticks <= 0)
			break;
		if (ticks > maxTicks)
			ticks = maxTicks;

		LARGE_INTEGER due;
		due.QuadPart = -ticks;
		SetWaitableTimerEx(timer, &due, 0, NULL, NULL, NULL, 0);
		WaitForSingleObject(timer, INFINITE);
		t = high_resolution_clock::now();
	}

	// spin
	while (high_resolution_clock::now() < target)
		YieldProcessor();
#endif
}

#ifdef WIN32
bool u::windows_toggle_suspend_process(DWORD pid, bool to_suspend) {
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE)
		return false;

	THREADENTRY32 te;
	te.dwSize = sizeof(te);

	if (!Thread32First(hSnapshot, &te)) {
		CloseHandle(hSnapshot);
		return false;
	}

	do {
		if (te.th32OwnerProcessID == pid) {
			HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
			if (hThread) {
				if (to_suspend)
					SuspendThread(hThread);
				else
					ResumeThread(hThread);
				CloseHandle(hThread);
			}
		}
	}
	while (Thread32Next(hSnapshot, &te));

	CloseHandle(hSnapshot);
	return true;
}
#endif
