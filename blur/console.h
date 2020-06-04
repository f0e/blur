#pragma once

#include <string>

class c_console {
private:
	const int console_w = 600;
	const int console_h = 400;
	const short font_w = 8;
	const short font_h = 16;

	int console_max_chars = 0;

public:
	void print_center(std::string string);
	void print_blank_line(int amount = 1);
	void print_line(int pad = 1);

	void set_font();

	void center_console();
};

extern c_console console;