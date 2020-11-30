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
		std::string video_path;
		if (argc <= 1) {
			console.print_center("waiting for input, drag a video onto this window.");

			// wait for the user to drop a file onto the window
			video_path = console.wait_for_dropped_file();
		}
		else {
			video_path = argv[1];
		}

		// check the file exists
		if (!std::filesystem::exists(video_path))
			throw std::exception("video couldn't be opened (wrong path?)");

		// set working directory
		std::filesystem::current_path(std::filesystem::path(video_path).parent_path());

		// set up filenames
		std::string video_name = std::filesystem::path(video_path).stem().string();
		std::string input_filename = std::filesystem::path(video_path).filename().string();
		std::string output_filename = video_name + " - blur.mp4";

		console.print_center(fmt::format("opening {} for processing,", input_filename));
		console.print_center(fmt::format("writing to {}", output_filename));
		console.print_line();

		// check if the output path already contains a video
		if (std::filesystem::exists(output_filename)) {
			console.print_center("destination file already exists, overwrite? (y/n)");
			console.print_blank_line();

			char choice = console.get_char();

			if (tolower(choice) == 'y') {
				std::filesystem::remove(output_filename);

				console.print_center("overwriting file");
				console.print_blank_line();
			}
			else {
				throw std::exception("not overwriting file");
			}
		}

		// parse config file
		blur_settings settings = config.parse();

		console.print_center(fmt::format("your settings:"));
		console.print_center(fmt::format("- {} cores, {} threads -", settings.cpu_cores, settings.cpu_threads));
		console.print_center(fmt::format("- source {}fps video at {:.2f} timescale -", settings.input_fps, settings.timescale));
		if (settings.interpolate) console.print_center(fmt::format("- interpolated to {}fps -", settings.interpolated_fps, settings.interpolation_speed, settings.interpolation_tuning, settings.interpolation_algorithm));
		if (settings.blur) console.print_center(fmt::format("- motion blurred ({:d}%) -", static_cast<int>(settings.blur_amount * 100)));
		console.print_center(fmt::format("- rendered into {}fps with crf {} -", settings.output_fps, settings.crf));

		console.print_line();

		// create avisynth script
		avisynth.create(video_path, settings);

		// run avisynth script through ffmpeg
		console.print_center(fmt::format("starting render..."));
		console.print_blank_line();

		std::string preview_filename = avisynth.get_path() + "preview.jpg";

		auto ffmpeg_settings = ffmpeg.get_settings(settings, output_filename, preview_filename);

		// do preview
		if (settings.preview) {
			preview.start(preview_filename);
		}

		// run ffmpeg
		system(ffmpeg_settings.c_str());

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