#include "cli.h"

#ifdef _WIN32
using PathStr = std::wstring;
#else
using PathStr = std::string;
#endif

namespace {
	std::vector<std::filesystem::path> to_paths(const std::vector<PathStr>& strs) {
		auto view = strs | std::views::transform([](const PathStr& s) {
						return std::filesystem::path(s);
					});
		return { view.begin(), view.end() };
	}
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
	// fix logs on windows
#ifdef WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif

	CLI::App app{ "Add motion blur to videos" };

	std::vector<PathStr> input_strs;
	std::vector<PathStr> output_strs;
	std::vector<PathStr> config_path_strs;
	bool preview = false;
	bool verbose = false;

	app.add_option("-i,--input", input_strs, "Input file name(s)")->required();
	app.add_option("-o,--output", output_strs, "Output file name(s) (optional)");
	app.add_option("-c,--config-path", config_path_strs, "Manual configuration file path(s) (optional)");
	app.add_flag("-p,--preview", preview, "Enable preview");
	app.add_flag("-v,--verbose", verbose, "Verbose mode");

	CLI11_PARSE(app, argc, argv);

	auto inputs = to_paths(input_strs);
	auto outputs = to_paths(output_strs);
	auto config_paths = to_paths(config_path_strs);

	cli::run(inputs, outputs, config_paths, preview, verbose);

	return 0;
}
