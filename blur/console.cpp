#include "console.h"

#include "includes.h"

c_console console;

void c_console::setup() {
	SetConsoleTitleA("tekno blur");

	center_console();
	set_font();

	show_cursor(false);
}

void c_console::print_center(std::string string) {
	const int padding = (console_max_chars - string.size()) / 2;
	const int scrollbar_fix = 1; // fixes the look of centering

	printf("%*s%s\n", padding + scrollbar_fix, "", string.c_str());

	current_line++;
}

void c_console::print_blank_line(int amount) {
	for (int i = 0; i < amount; i++) {
		printf("\n");

		current_line++;
	}
}

void c_console::print_line(int pad) {
	for (int i = 0; i < pad; i++)
		print_blank_line();

	printf(" ");

	for (int i = 1; i < console_max_chars; i++)
		printf("-");

	print_blank_line();

	for (int i = 0; i < pad; i++)
		print_blank_line();
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

void c_console::center_console() {
	const HWND desktop = GetDesktopWindow();
	const HWND console = GetConsoleWindow();

	// get window position information
	RECT console_rect;
	GetWindowRect(console, &console_rect);

	// get desktop position information
	RECT desktop_rect;
	GetWindowRect(desktop, &desktop_rect);

	// get positions
	const int console_x = desktop_rect.left + ((desktop_rect.right - desktop_rect.left) / 2) - console_w / 2;
	const int console_y = desktop_rect.top + ((desktop_rect.bottom - desktop_rect.top) / 2) - console_h / 2;

	// center and resize window
	MoveWindow(console, console_x, console_y, console_w, console_h, TRUE);

	// get max console chars
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	console_max_chars = static_cast<int>(csbi.srWindow.Right - csbi.srWindow.Left);
	console_max_lines = static_cast<int>(csbi.srWindow.Bottom - csbi.srWindow.Top);

	// disable resizing
	SetWindowLong(console, GWL_STYLE, GetWindowLong(console, GWL_STYLE) & ~WS_MAXIMIZEBOX & ~WS_SIZEBOX);
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