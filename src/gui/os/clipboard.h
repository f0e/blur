#pragma once

#include <filesystem>

namespace os::clipboard {
	// put the file itself on the clipboard, so it can be pasted into a file browser, chat app etc.
	// (SDL only does text, and every platform wants its own format for this)
	bool copy_file(const std::filesystem::path& path);
}
