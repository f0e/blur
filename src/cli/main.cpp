#include "cli.h"

int main(int argc, char* argv[]) {
	CLI::App app{ "Add motion blur to videos" };

	std::vector<std::string> inputs;
	std::vector<std::string> outputs;
	std::vector<std::string> config_paths;
	bool preview = false;
	bool verbose = false;

	app.add_option("-i,--input", inputs, "Input file name(s)")->required();
	app.add_option("-o,--output", outputs, "Output file name(s) (optional)");
	app.add_option("-c,--config-path", config_paths, "Manual configuration file path(s) (optional)");
	app.add_flag("-p,--preview", preview, "Enable preview");
	app.add_flag("-v,--verbose", verbose, "Verbose mode");

	CLI11_PARSE(app, argc, argv);

	cli::run(inputs, outputs, config_paths, preview, verbose);

	return 0;
}
