#include "cli.h"

int main(int argc, char* argv[]) {
	cxxopts::Options options("blur", "Add motion blur to videos");
	options.add_options()
		("h,help", "Print usage")
		("i,input", "Input file name(s)", cxxopts::value<std::vector<std::string>>())
		("o,output", "Output file name(s) (optional)", cxxopts::value<std::vector<std::string>>())
		("c,config-path", "Manual configuration file path(s) (optional)", cxxopts::value<std::vector<std::string>>())
		("p,preview", "Enable preview", cxxopts::value<bool>())
		("v,verbose", "Verbose mode", cxxopts::value<bool>())
		;

	try {
		auto result = options.parse(argc, argv);

		if (result.count("help")) {
			std::cout << options.help() << std::endl;
			return 0;
		}

		cli::run(result);
	}
	catch (const cxxopts::exceptions::exception&) {
		std::cout << "Failed to parse arguments, use -h or --help for help.";
	}

	return 0;
}