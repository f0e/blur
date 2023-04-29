#include "blur.h"

#include "preview.h"

bool c_blur::initialise(bool _verbose, bool _using_preview) {
	path = std::filesystem::path(helpers::get_executable_path()).parent_path(); // folder the exe is in
	used_installer = std::filesystem::exists(path / "lib\\vapoursynth\\vspipe.exe")
		&& std::filesystem::exists(path / "lib\\ffmpeg\\ffmpeg.exe");

	if (!used_installer) {
		// didn't use installer, check if dependencies were installed
		if (!helpers::detect_command("ffmpeg")) {
			printf("FFmpeg could not be found\n");
			return false;
		}

		if (!helpers::detect_command("python")) {
			printf("Python could not be found\n");
			return false;
		}

		if (!helpers::detect_command("vspipe")) {
			printf("VapourSynth could not be found\n");
			return false;
		}
	}

	verbose = _verbose;
	using_preview = _using_preview;

	return true;
}

std::filesystem::path c_blur::create_temp_path(const std::filesystem::path& video_path) {
	std::filesystem::path temp_path = video_path / "blur_temp";

	// check if the path already exists
	if (!std::filesystem::exists(temp_path)) {
		// try create the path
		if (!std::filesystem::create_directory(temp_path))
			throw std::exception("failed to create temporary path");
	}

	helpers::set_hidden(temp_path);

	return temp_path;
}

void c_blur::remove_temp_path(const std::filesystem::path& path) {
	// check if the path doesn't already exist
	if (!std::filesystem::exists(path))
		return;

	// remove the path and all the files inside
	std::filesystem::remove_all(path);
}