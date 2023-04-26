#pragma once

namespace console {
	inline bool initialised = false;

	inline FILE* stream;

	bool init();

	void print(const std::string& text);
	void print(const std::wstring& text);

	void print_line();
	void print_blank_line();
}
