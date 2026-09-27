#include "paths.h"

std::filesystem::path paths::get_resources_path() {
#ifdef __APPLE__
	// Resources path if part of a macos bundle
	CFBundleRef bundle = CFBundleGetMainBundle();
	if (bundle) {
		CFURLRef resources_url = CFBundleCopyResourcesDirectoryURL(bundle);
		if (resources_url) {
			std::array<char, PATH_MAX> path{};
			if (CFURLGetFileSystemRepresentation(
					resources_url, static_cast<Boolean>(true), (UInt8*)path.data(), path.size() // NOLINT
				))
			{
				CFRelease(resources_url);
				return path.data();
			}
			CFRelease(resources_url);
		}
	}
#endif

	// binary path otherwise
	return std::filesystem::path(u::get_executable_path()).parent_path();
}

std::filesystem::path paths::get_settings_path() {
	std::filesystem::path settings_path;
	const std::string app_name = "blur";

#if defined(_WIN32) || defined(_WIN64)
	// Windows: %APPDATA%\blur
	const char* app_data = std::getenv("APPDATA");
	if (app_data) {
		settings_path = std::filesystem::path(app_data) / app_name;
	}
	else {
		// Fallback if APPDATA is not available
		const char* user_profile = std::getenv("USERPROFILE");
		if (user_profile) {
			settings_path = std::filesystem::path(user_profile) / "AppData" / "Roaming" / app_name;
		}
	}
#elif defined(__APPLE__)
	// macOS: ~/Library/Application Support/blur
	const char* home = std::getenv("HOME");
	if (home) {
		settings_path = std::filesystem::path(home) / "Library" / "Application Support" / app_name;
	}
#else
	// Linux/Unix: ~/.config/blur (XDG convention)
	const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME");
	if (xdg_config_home && xdg_config_home[0] != '\0') {
		settings_path = std::filesystem::path(xdg_config_home) / app_name;
	}
	else {
		const char* home = std::getenv("HOME");
		if (home) {
			settings_path = std::filesystem::path(home) / ".config" / app_name;
		}
	}
#endif

	// Create directories if they don't exist
	if (!settings_path.empty()) {
		std::error_code ec;
		std::filesystem::create_directories(settings_path, ec);
	}

	return settings_path;
}
