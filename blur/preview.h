#pragma once

#include <Windows.h>

#include <string>
#include <thread>

class c_preview {
private:
	const int preview_width = 1000;
	const int preview_height = 563;

	std::string preview_filename;
	bool preview_disabled = false;
	bool preview_open = false;

	HWND hwnd;
	bool drawing = false;

	bool drew_image = false;

	int window_ratio_x;
	int window_ratio_y;
	int g_window_adjust_x;
	int g_window_adjust_y;

	std::unique_ptr<std::thread> window_thread_ptr;
	std::unique_ptr<std::thread> watch_thread_ptr;

private:
	void resize(int edge, RECT& rect);
	static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	void create_window();
	void watch_preview();

public:
	void start(const std::string& filename);
	void stop();
	void disable();
};

inline c_preview preview;