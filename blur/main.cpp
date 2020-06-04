#include "config.h"

#include "includes.h"

#include <thread>

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
		console.print_center(fmt::format("{}", line));

	console.print_line();

	try {
		// get input video
		if (argc <= 1)
			throw std::exception("drag a video onto the exe");

		std::string video_path = argv[1];
		std::string video_name = std::filesystem::path(video_path).stem().string();
		std::string output_name = video_name + " - blur.mp4";

		// check if the output path already contains a video
		if (std::filesystem::exists(output_name)) {
			console.print_center("destination file already exists, overwrite? (y/n)");

			console.print_blank_line();

			console.center_cursor();
			console.show_cursor(true);

			char choice = _getch();

			console.show_cursor(false);
			console.reset_cursor();

			if (tolower(choice) == 'y') {
				std::filesystem::remove(output_name);
			}
			else {
				throw std::exception("can't write output file");
			}
		}

		// parse config file
		blur_settings settings = config.parse();

		console.print_center(fmt::format("your settings:"));
		console.print_center(fmt::format("- {} cores, {} threads -", settings.cpu_cores, settings.cpu_threads));
		console.print_center(fmt::format("- source {}fps video at {:.2f} timescale -", settings.input_fps, settings.timescale));
		if (settings.interpolate) console.print_center(fmt::format("- interpolated to {}fps -", settings.interpolated_fps));
		if (settings.blur) console.print_center(fmt::format("- blended with {:.2f} exposure -", settings.exposure));
		console.print_center(fmt::format("- rendered into {}fps -", settings.output_fps));

		console.print_line();

		// create avisynth script
		avisynth.create(video_path, settings);

		// run avisynth script through ffmpeg
		console.print_center(fmt::format("starting render..."));

		console.print_blank_line();

		system(ffmpeg.get_settings(video_name, output_name).c_str());

		console.print_blank_line();

		console.print_center(fmt::format("finished rendering video"));
	}
	catch (const std::exception& e) {
		console.print_center(e.what());
	}

	console.print_line();

	// delete the avisynth script
	avisynth.erase();

	// wait for keypress
	console.print_center(fmt::format("press any key to exit"));
	_getch();
}