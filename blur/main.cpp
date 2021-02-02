#include "includes.h"

int main(int argc, char* argv[]) {
	// setup console
	console.setup();

	// print art
	const std::vector<const char*> art {
		"    _/        _/                   ",
		"   _/_/_/    _/  _/    _/  _/  _/_/",
		"  _/    _/  _/  _/    _/  _/_/     ",
		" _/    _/  _/  _/    _/  _/        ",
		"_/_/_/    _/    _/_/_/  _/         ",
	};

	console.print_blank_line();

	for (const auto& line : art)
		console.print_center(line);

	console.print_line();

	blur.run(argc, argv);
}