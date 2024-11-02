#include "console.h"

#ifdef _WIN32
#	include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#	include <unistd.h>
#	include <termios.h>
#	include <sys/ioctl.h>
#endif

#ifdef _WIN32
void console::cleanup() {
	console::close();
}
#endif

bool console::init() {
	if (initialised) {
		return false;
	}

#ifdef _WIN32
	// Windows-specific console initialization
	if (!AllocConsole()) {
		return false;
	}

	// Register cleanup handler
	atexit(cleanup);

	// Redirect stdout to the console
	if (freopen_s(&stream, "CONOUT$", "w", stdout) != 0) {
		FreeConsole();
		return false;
	}

#elif defined(__APPLE__) || defined(__linux__)
	// Unix-like systems typically have a console by default
	// Just need to check if stdout is connected to a terminal
	if (!isatty(STDOUT_FILENO)) {
		// If stdout is not a terminal, try to reconnect it
		stream = freopen("/dev/tty", "w", stdout);
		if (!stream) {
			return false;
		}
	}

	// Configure terminal settings if needed
	struct termios term;
	if (tcgetattr(STDOUT_FILENO, &term) == 0) {
		// Enable echo and canonical mode
		term.c_lflag |= (ECHO | ICANON);
		tcsetattr(STDOUT_FILENO, TCSANOW, &term);
	}

	// Try to ensure the terminal is in a good state
	printf("\033[0m"); // Reset all attributes
	fflush(stdout);
#endif

	initialised = true;
	return true;
}

bool console::close() {
	if (!initialised) {
		return false;
	}

#ifdef _WIN32
	// Windows-specific console cleanup
	if (stream) {
		fclose(stream);
		stream = nullptr;
	}

	if (!FreeConsole()) {
		return false;
	}

#elif defined(__APPLE__) || defined(__linux__)
	// Unix-like systems: restore terminal settings
	if (isatty(STDOUT_FILENO)) {
		struct termios term;
		if (tcgetattr(STDOUT_FILENO, &term) == 0) {
			// Restore default terminal settings if needed
			term.c_lflag |= (ECHO | ICANON);
			tcsetattr(STDOUT_FILENO, TCSANOW, &term);
		}
	}

	// Close redirected stream if we created one
	if (stream) {
		fclose(stream);
		stream = nullptr;
	}
#endif

	initialised = false;
	return true;
}
