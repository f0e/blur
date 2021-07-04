#include "includes.h"

std::vector<std::string> c_console::get_text_lines(const std::string& text, int max_width) {
	// first, check if we're even going to overflow. if not, just return a single line with the text
	int width = text.size();
	if (width < max_width)
		return { text.data() };

	std::vector<std::string> words = helpers::split_string(text.data(), " ");

	std::vector<std::string> lines = {};
	std::string current_line = "";

	// go through words and build lines
	for (auto& word : words) {
		std::string new_line = current_line == ""
			? word
			: current_line + " " + word;

		// check if the line's going to overflow
		int width = new_line.size();
		if (width < max_width) {
			// line won't overflow, add to the current line
			current_line = new_line;
		}
		else {
			// line will overflow, start a new one
			if (current_line != "")
				lines.push_back(current_line);

			// check that making a new line is fine
			int new_line_width = word.size();
			if (new_line_width < max_width) {
				current_line = word;
				continue;
			}

			// the new line will be too long as well, do extra clipping
			std::string clipped_word = "";
			for (const auto& c : word) {
				clipped_word += c;

				// see if it's gonna overflow
				int element_text_size = clipped_word.size();
				if (element_text_size > max_width) {
					// make a new line and reset
					lines.push_back(clipped_word);
					clipped_word = "";
				}
			}

			// add the last clip
			if (clipped_word != "")
				lines.push_back(clipped_word);
		}
	}

	// add final line
	if (current_line != "")
		lines.push_back(current_line);

	return lines;
}

void c_console::setup() {
	hwnd = GetConsoleWindow();

	if (blur.using_ui) {
		SetConsoleTitleA(name);

		center_console();
		set_font();

		show_cursor(false);
	}
}

void c_console::print(const std::string& string) {
	if (blur.using_ui) {
		// split into lines
		auto lines = get_text_lines(string, console_max_chars);

		for (auto& line : lines) {
			const int padding = (console_max_chars - line.size()) / 2;
			const int scrollbar_fix = 1; // fixes the look of centering

			printf("%*s%s\n", padding + scrollbar_fix, "", line.data());

			current_line++;
		}
	}
	else {
		std::cout << string << "\n";
	}
}

void c_console::print_blank_line(int amount) {
	for (int i = 0; i < amount; i++) {
		printf("\n");

		current_line++;
	}
}

void c_console::print_line(int pad) {
	if (blur.using_ui) {
		for (int i = 0; i < pad; i++)
			print_blank_line();

		printf(" ");

		for (int i = 1; i < console_max_chars; i++)
			printf("-");

		print_blank_line();

		for (int i = 0; i < pad; i++)
			print_blank_line();
	}
	else {
		printf("-----------------------------------\n");
	}
}

void c_console::set_font() {
	CONSOLE_FONT_INFOEX cfi;
	cfi.cbSize = sizeof cfi;
	cfi.nFont = 0;
	cfi.dwFontSize.X = font_w;
	cfi.dwFontSize.Y = font_h;
	cfi.FontFamily = FF_DONTCARE;
	cfi.FontWeight = FW_NORMAL;
	wcscpy_s(cfi.FaceName, L"Consolas");
	SetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), FALSE, &cfi);
}

RECT c_console::get_console_position() {
	const HWND desktop = GetDesktopWindow();

	RECT console_rect;
	GetWindowRect(hwnd, &console_rect);

	return console_rect;
}

void c_console::center_console() {
	const HWND desktop = GetDesktopWindow();

	// get window position information
	RECT console_rect;
	GetWindowRect(hwnd, &console_rect);

	// get desktop position information
	RECT desktop_rect;
	GetWindowRect(desktop, &desktop_rect);

	// get positions
	const int console_x = desktop_rect.left + ((desktop_rect.right - desktop_rect.left) / 2) - console_w / 2;
	const int console_y = desktop_rect.top + ((desktop_rect.bottom - desktop_rect.top) / 2) - console_h / 2;

	// center and resize window
	MoveWindow(hwnd, console_x, console_y, console_w, console_h, TRUE);

	// get max console chars
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	console_max_chars = static_cast<int>(csbi.srWindow.Right - csbi.srWindow.Left);
	console_max_lines = static_cast<int>(csbi.srWindow.Bottom - csbi.srWindow.Top);

	// disable resizing
	SetWindowLong(hwnd, GWL_STYLE, GetWindowLong(hwnd, GWL_STYLE) & ~WS_MAXIMIZEBOX & ~WS_SIZEBOX);
}

void c_console::show_cursor(bool showing) {
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);

	CONSOLE_CURSOR_INFO cursorInfo;
	GetConsoleCursorInfo(handle, &cursorInfo);

	cursorInfo.bVisible = showing; // set the cursor visibility

	SetConsoleCursorInfo(handle, &cursorInfo);
}

void c_console::center_cursor() {
	COORD cursor_pos;
	cursor_pos.X = static_cast<int>(console_max_chars / 2);
	cursor_pos.Y = current_line;

	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleCursorPosition(handle, cursor_pos);
}

void c_console::reset_cursor() {
	COORD cursor_pos;
	cursor_pos.X = 0;
	cursor_pos.Y = current_line;

	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleCursorPosition(hConsole, cursor_pos);
}

char c_console::get_char() {
	// console.center_cursor();
	console.show_cursor(true);

	char choice = _getch();

	console.show_cursor(false);
	// console.reset_cursor();

	return choice;
}

std::string c_console::wait_for_dropped_file() { // https://stackoverflow.com/a/25484783
	char ch = _getch();

	std::string file_name;
	if (ch == '\"') { // path containing spaces. read til next '"' ...
		while ((ch = _getch()) != '\"')
			file_name += ch;
	}
	else { // path not containing spaces. read as long as chars are coming rapidly
		file_name += ch;

		while (_kbhit())
			file_name += _getch();
	}

	return file_name;
}