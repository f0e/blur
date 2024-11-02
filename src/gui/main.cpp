#include "tasks.h"
#include "gui.h"

int app_main(int argc, char* argv[]) {
	std::thread(tasks::run).detach();

	gui::run();

	return 0;
}
