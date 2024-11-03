#include "blur.h"

bool c_blur::initialise(bool _verbose, bool _using_preview) {
	path = std::filesystem::path(helpers::get_executable_path()).parent_path(); // folder the exe is in
	used_installer = std::filesystem::exists(path / "lib\\vapoursynth\\vspipe.exe") && std::filesystem::exists(path / "lib\\ffmpeg\\ffmpeg.exe");

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

	std::atexit([] {
		rendering.stop_rendering();
		blur.cleanup();
	});

	return true;
}

void c_blur::cleanup() {
	for (const auto& path : created_temp_paths) {
		if (!std::filesystem::exists(path))
			continue;

		std::filesystem::remove_all(path);
	}
}
