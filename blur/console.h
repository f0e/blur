#pragma once

#include <string>

namespace console {
	void print_center(std::string string);
	void print_blank_line(int amount = 1);
	void print_line(int pad = 1);
	void set_font();
	void center_console();
}