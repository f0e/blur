#pragma once

#include <filesystem>

namespace os::clipboard {
	bool copy_file(const std::filesystem::path& path);
}
