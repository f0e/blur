#include "preview_files.h"

namespace {
	constexpr std::string_view TEMP_PREFIX = "blur-preview-";

	unsigned long current_pid() {
#ifdef _WIN32
		return GetCurrentProcessId();
#else
		return getpid();
#endif
	}

	bool process_running(unsigned long pid) {
#ifdef _WIN32
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

		// it's there, it just isn't ours to look at
		if (!process)
			return GetLastError() == ERROR_ACCESS_DENIED;

		DWORD exit_code = 0;
		bool running = GetExitCodeProcess(process, &exit_code) != 0 && exit_code == STILL_ACTIVE;
		CloseHandle(process);
		return running;
#else
		return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
	}

	std::filesystem::path temp_folder() {
		return std::filesystem::temp_directory_path() / std::format("{}{}", TEMP_PREFIX, current_pid());
	}
}

std::filesystem::path preview_files::new_path(const std::string& extension) {
	static std::atomic<int> count = 0;

	auto folder = temp_folder();

	std::error_code ec;
	std::filesystem::create_directories(folder, ec);

	return folder / std::format("{}.{}", ++count, extension);
}

void preview_files::remove_all() {
	std::error_code ec;
	std::filesystem::remove_all(temp_folder(), ec);

	if (ec)
		u::log_error("failed to remove preview files: {}", ec.message());
}

void preview_files::remove_stale() {
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path(), ec)) {
		auto name = u::path_to_string(entry.path().filename());
		if (!name.starts_with(TEMP_PREFIX))
			continue;

		// folders are named for the process they're from. older versions left loose files, named the same way
		unsigned long pid = 0;
		auto digits = std::string_view(name).substr(TEMP_PREFIX.size());
		auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), pid);
		if (parsed.ec != std::errc() || pid == current_pid() || process_running(pid))
			continue;

		std::error_code remove_error;
		std::filesystem::remove_all(entry.path(), remove_error);
	}
}
