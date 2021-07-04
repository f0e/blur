#pragma once

#include <Windows.h>

#include <string>
#include <vector>

class c_console {
public:
	HWND hwnd;

private:
	const char* name = "tekno's blur";

	const int console_w = 650;
	const int console_h = 400;
	const short font_w = 8;
	const short font_h = 16;

	int console_max_chars = 0;
	int console_max_lines = 0;

	int current_line = 0;

private:
	static std::vector<std::string> get_text_lines(const std::string& text, int max_width);

public:
	void setup();

	void print(const std::string& string);
	void print_blank_line(int amount = 1);
	void print_line(int pad = 1);

	void set_font();

	RECT get_console_position();

	void center_console();

	void show_cursor(bool showing);
	void center_cursor();
	void reset_cursor();

	char get_char();

	std::string wait_for_dropped_file();
};

inline c_console console;