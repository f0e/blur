#include "clipboard.h"

#ifndef __APPLE__

#	ifdef _WIN32

#		include <shellapi.h>

bool os::clipboard::copy_file(const std::filesystem::path& path) {
	std::wstring wide_path = path.wstring();

	// CF_HDROP wants a DROPFILES header followed by the paths, double null terminated
	size_t paths_bytes = (wide_path.size() + 2) * sizeof(wchar_t);

	HGLOBAL global = GlobalAlloc(GHND, sizeof(DROPFILES) + paths_bytes);
	if (!global)
		return false;

	auto* drop_files = static_cast<DROPFILES*>(GlobalLock(global));
	if (!drop_files) {
		GlobalFree(global);
		return false;
	}

	drop_files->pFiles = sizeof(DROPFILES);
	drop_files->fWide = TRUE;

	auto* paths = reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(drop_files) + sizeof(DROPFILES));
	std::copy(wide_path.begin(), wide_path.end(), paths);
	paths[wide_path.size()] = L'\0';
	paths[wide_path.size() + 1] = L'\0';

	GlobalUnlock(global);

	if (!OpenClipboard(nullptr)) {
		GlobalFree(global);
		return false;
	}

	EmptyClipboard();

	if (!SetClipboardData(CF_HDROP, global)) {
		CloseClipboard();
		GlobalFree(global);
		return false;
	}

	CloseClipboard(); // the clipboard owns the memory now

	return true;
}

#	else

namespace {
	// SDL hands the data back out for whichever mime type the paste asks for
	const void* uri_list_callback(void* userdata, const char* mime_type, size_t* size) {
		auto* uri = static_cast<std::string*>(userdata);
		*size = uri->size();
		return uri->data();
	}

	void uri_list_cleanup(void* userdata) {
		delete static_cast<std::string*>(userdata);
	}
}

bool os::clipboard::copy_file(const std::filesystem::path& path) {
	// file managers on linux take a uri list
	auto* uri = new std::string(std::format("file://{}\r\n", u::path_to_string(path)));

	const char* mime_types[] = { "text/uri-list" };

	if (!SDL_SetClipboardData(uri_list_callback, uri_list_cleanup, uri, mime_types, 1)) {
		delete uri;
		return false;
	}

	return true;
}

#	endif

#endif
