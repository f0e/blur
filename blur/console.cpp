#include "console.h"

#include "includes.h"

const int console_w = 600;
const int console_h = 400;
const SHORT font_w = 8;
const SHORT font_h = 16;

int console_max_chars = 0;

void console::print_center(std::string string) {
	const int padding = (console_max_chars - string.size()) / 2;
	const int scrollbar_fix = 1; // fixes the look of centering

	printf("%*s%s\n", padding + scrollbar_fix, "", string.c_str());
}

void console::print_blank_line(int amount) {
	for (int i = 0; i < amount; i++)
		printf("\n");
}

void console::print_line(int pad) {
	for (int i = 0; i < pad; i++)
		print_blank_line();

	printf(" ");

	for (int i = 1; i < console_max_chars; i++)
		printf("-");

	print_blank_line();

	for (int i = 0; i < pad; i++)
		print_blank_line();
}

void console::set_font() {
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

void console::center_console() {
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

	// disable resizing
	SetWindowLong(console, GWL_STYLE, GetWindowLong(console, GWL_STYLE) & ~WS_MAXIMIZEBOX & ~WS_SIZEBOX);
}