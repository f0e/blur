#include "includes.h"

#include <cxxopts/cxxopts.hpp>

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