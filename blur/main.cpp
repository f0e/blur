#include "gui.h"

#include "console.h"

#include <shellapi.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow) {
#ifdef _DEBUG
	// initialise debug console
	console::init();
#endif

	// queue videos passed as arguments
	int num_args;
	LPWSTR* arg_list = CommandLineToArgvW(GetCommandLineW(), &num_args);
	if (arg_list) {
		for (int i = 1; i < num_args; i++) {
			std::wstring path = arg_list[i];
			if (!std::filesystem::exists(path))
				continue;

			rendering.queue_render(std::make_shared<c_render>(path));
		}
	}

	// run gui in another thread
	std::thread(gui::run).detach();

	// run main loop
	blur.run_gui();

	return 0;
}

/*
int main(int argc, char* argv[]) {
	cxxopts::Options options("blur", "Add motion blur to videos");
	options.add_options()
		("h,help", "Print usage")
		("i,input", "Input file name(s)", cxxopts::value<std::vector<std::string>>())
		("o,output", "Output file name(s) (optional)", cxxopts::value<std::vector<std::string>>())
		("c,config-path", "Manual configuration file path(s) (optional)", cxxopts::value<std::vector<std::string>>())
		("n,noui", "Disable user interface", cxxopts::value<bool>())
		("p,nopreview", "Disable preview", cxxopts::value<bool>())
		("v,verbose", "Verbose mode", cxxopts::value<bool>())
		;

	try {
		auto result = options.parse(argc, argv);

		if (result.count("help")) {
			std::cout << options.help() << std::endl;
			return 0;
		}

		blur.run(argc, argv, result);
	}
	catch (const cxxopts::OptionParseException&) {
		std::cout << "Failed to parse arguments, use -h or --help for help.";
	}

	return 0;
}
*/