#pragma once

#include <string>
#include <windows.h>

class c_preview {
public:
	std::string preview_filename;

	HWND hwnd;
	bool drawing = false;

public:
	void start(std::string_view filename);
	void stop();
};

extern c_preview preview;