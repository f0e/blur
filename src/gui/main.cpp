#include "tasks.h"
#include "gui.h"

#include <SDL3/SDL_main.h> // https://wiki.libsdl.org/SDL3/SDL_main#remarks

int main(int argc, char* argv[]) {
	// fix logs on windows
#ifdef WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif

	std::vector<std::string> arguments(argv + 1, argv + argc);

	std::thread(tasks::run, arguments).detach();

	return gui::run();
}
