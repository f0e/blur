#pragma once

#include <filesystem>

namespace os::file_browser {
	// Open the system file browser at path and select the file when the platform supports it.
	bool reveal_file(const std::filesystem::path& path);
}
