#pragma once

#include <string>
#include <windows.h>

class c_preview {
public:
	std::string preview_filename;
	const int preview_width = 1000;
	const int preview_height = 563;

	HWND hwnd;
	bool drawing = false;

public:
	void start(std::string_view filename);
	void stop();
};

extern c_preview preview;