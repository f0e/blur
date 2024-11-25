#include "tasks.h"
#include "gui.h"

int app_main(int argc, char* argv[]) { // NOLINT
	std::thread(tasks::run).detach();

	gui::run();

	return 0;
}
