#include "console.h"

#include <fmt/chrono.h>

#include <fcntl.h>
#include <io.h>
#include <stdio.h>

bool console::init() {
#ifndef _DEBUG
	return true;
#endif

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

void console::print(const std::string& text) {
	std::cout << text << "\n";
}

void console::print(const std::wstring& text) {
	std::wcout << text << "\n";
}

void console::print_line() {
	printf("--------------------------\n");
}

void console::print_blank_line() {
	printf("\n");
}