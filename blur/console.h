#pragma once

#include <string>

class c_console {
private:
	const int console_w = 650;
	const int console_h = 400;
	const short font_w = 8;
	const short font_h = 16;

	int console_max_chars = 0;
	int console_max_lines = 0;

	int current_line = 0;

public:
	void setup();

	void print_center(std::string string);
	void print_blank_line(int amount = 1);
	void print_line(int pad = 1);

	void set_font();

	void center_console();

	void show_cursor(bool showing);
	void center_cursor();
	void reset_cursor();
};

extern c_console console;