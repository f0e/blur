#include "file_browser.h"

#include <cstdint>
#include <format>
#include <string>

#ifndef __APPLE__

#	ifdef _WIN32

#		include <shellapi.h>

bool os::file_browser::reveal_file(const std::filesystem::path& path) {
	std::wstring arguments = std::format(L"/select,\"{}\"", path.wstring());
	auto result = reinterpret_cast<intptr_t>(
		ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL)
	);

	return result > 32;
}

#	else

#		include <SDL3/SDL.h>

bool os::file_browser::reveal_file(const std::filesystem::path& path) {
	// there is no universally supported selection API on Linux, so at least open the containing directory
	std::string url = std::format("file://{}", u::path_to_string(path.parent_path()));
	return SDL_OpenURL(url.c_str());
}

#	endif

#endif
