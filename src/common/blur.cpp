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

bool c_blur::create_temp_path() {
	if (!blur.temp_path.empty())
		return true;

	temp_path = std::filesystem::temp_directory_path() / "blur";

	if (!std::filesystem::exists(temp_path)) {
		if (!std::filesystem::create_directory(temp_path))
			return false;
	}

	// also remove temp path on program exit
	std::atexit([] {
		blur.remove_temp_path();
	});

	return true;
}

bool c_blur::remove_temp_path() {
	if (blur.temp_path.empty())
		return true;

	if (!std::filesystem::exists(blur.temp_path))
		return true;

	std::filesystem::remove_all(blur.temp_path);

	blur.temp_path.clear();

	return true;
}