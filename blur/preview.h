#pragma once

#include <Windows.h>

#include <string>
#include <thread>

class c_preview {
public:
	std::string preview_filename;
	bool preview_disabled = false;
	bool preview_open = false;

	const int preview_width = 1000;
	const int preview_height = 563;

	HWND hwnd;
	bool drawing = false;

	std::unique_ptr<std::thread> window_thread_ptr;
	std::unique_ptr<std::thread> watch_thread_ptr;

public:
	void start(std::string_view filename);
	void stop();
	void disable();
};

inline c_preview preview;