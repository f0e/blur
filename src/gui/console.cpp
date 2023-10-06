#include "console.h"

bool console::init() {
	if (initialised)
		return false;

	if (!AllocConsole())
		return false;

	atexit([]() {
		console::close();
	});

	if (freopen_s(&stream, "CONOUT$", "w", stdout) != NO_ERROR)
		return false;

	initialised = true;
	return true;
}

bool console::close() {
	if (!initialised)
		return false;

	if (!FreeConsole())
		return false;

	if (stream) {
		if (fclose(stream) != NO_ERROR)
			return false;
	}

	initialised = false;
	return true;
}