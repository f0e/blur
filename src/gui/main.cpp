#include "tasks.h"
#include "gui.h"

int app_main(int argc, char* argv[]) { // NOLINT
#ifdef _DEBUG
	base::SystemConsole system_console;
	system_console.prepareShell();
#endif

	std::vector<std::string> arguments(argv + 1, argv + argc);

	// redundant since add_files checks paths too so i could just pass arguments to it, but id rather keep the logic
	// here for the clarity
	std::vector<std::string> passed_file_paths;
	for (const auto& file_path_str : arguments) {
		std::filesystem::path file_path(file_path_str);

		if (!file_path.empty() && std::filesystem::exists(file_path))
			passed_file_paths.push_back(file_path_str);
	}

	std::thread(tasks::run).detach();

	tasks::add_files(passed_file_paths);

	gui::run();

	return 0;
}
