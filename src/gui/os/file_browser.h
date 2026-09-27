#pragma once

#include <filesystem>

namespace os::file_browser {
	bool reveal_file(const std::filesystem::path& path);
}
