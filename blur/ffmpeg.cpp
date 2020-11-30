#include "ffmpeg.h"

#include "includes.h"

c_ffmpeg ffmpeg;

bool detect_command(std::string command) {
	std::string find_ffmpeg_command = "where /q " + command;

	return system(find_ffmpeg_command.c_str()) == 0;
}

std::string c_ffmpeg::get_settings(const blur_settings& settings, const std::string& output_name, const std::string& preview_name) {
	// check if ffmpeg is installed
	if (!detect_command("ffmpeg"))
		throw std::exception("FFmpeg could not be found");

	std::vector<std::string> ffmpeg_settings {
		"ffmpeg",
		"-loglevel error -hide_banner -stats", // only show progress
		fmt::format("-i {}", avisynth.get_filename()), // input file
		"-c:v libx264 -pix_fmt yuv420p", // render format settings
		fmt::format("-preset superfast -crf {}", settings.crf), // main render settings
		fmt::format("\"{}\"", output_name) // output file
	};

	if (settings.preview) {
		ffmpeg_settings.push_back(
			fmt::format("-q:v 10 -update 1 -atomic_writing 1 -y \"{}\"", preview_name)
		);
	}

	std::string ffmpeg_command = "";
	for (const auto& ffmpeg_setting : ffmpeg_settings)
		ffmpeg_command += " " + ffmpeg_setting;

	return ffmpeg_command;
}
