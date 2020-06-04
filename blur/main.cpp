#include "config.h"

#include "includes.h"

#include <thread>

int main(int argc, char* argv[]) {
	// setup console
	SetConsoleTitleA("tekno blur");
	console.center_console();
	console.set_font();

	// print art
	const std::vector<const char*> art{
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
		const static std::vector<std::string> ffmpeg_settings = {
			"ffmpeg",
			"-v warning -hide_banner -stats", // only show progress
			"-i " + avisynth.get_filename(), // input file
			"-c:v libx264 -pix_fmt yuv420p", // render format settings
			"-preset superfast -crf 18", // main render settings
			"\"" + video_name + " - blur.mp4\"" // output file
		};

		std::string ffmpeg_command = "";
		for (const auto& ffmpeg_setting : ffmpeg_settings)
			ffmpeg_command += " " + ffmpeg_setting;

		console.print_center(fmt::format("starting render..."));

		console.print_blank_line();
		system(ffmpeg_command.c_str());
		console.print_blank_line();

		console.print_center(fmt::format("finished rendering video"));
	}
	catch (std::exception e) {
		console.print_center(e.what());
	}

	console.print_line();

	// delete the avisynth script
	avisynth.erase();

	// wait for keypress
	console.print_center(fmt::format("press any key to exit"));
	_getch();
}