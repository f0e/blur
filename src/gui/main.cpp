#include "tasks.h"
#include "gui.h"

int app_main(int argc, char* argv[]) { // NOLINT
#ifdef _DEBUG
	base::SystemConsole system_console;
	system_console.prepareShell();
#endif

	std::vector<std::string> arguments(argv + 1, argv + argc);

	std::thread(tasks::run, arguments).detach();

	gui::run();

	return 0;
}
