#include "console.h"

#include <fmt/chrono.h>

#include <fcntl.h>
#include <io.h>
#include <stdio.h>

bool console::init() {
	if (!AllocConsole())
		return false;

	atexit([]() {
		FreeConsole();
	});

	if (freopen_s(&stream, "CONOUT$", "w", stdout) != 0)
		return false;

	atexit([]() {
		fclose(stream);
	});

	initialised = true;

	return true;
}