#include "blur.h"
#include "common/helpers.h"

bool c_blur::initialise(bool _verbose, bool _using_preview) {
	path = std::filesystem::path(helpers::get_executable_path()).parent_path(); // folder the exe is in
	used_installer = std::filesystem::exists(path / "lib\\vapoursynth\\vspipe.exe") &&
	                 std::filesystem::exists(path / "lib\\ffmpeg\\ffmpeg.exe");

	if (!used_installer) {
		// didn't use installer, check if dependencies are installed
		if (auto _ffmpeg_path = helpers::get_program_path("ffmpeg")) {
			ffmpeg_path = *_ffmpeg_path;
		}
		else {
			printf("FFmpeg could not be found\n");
			return false;
		}

		if (auto _vspipe_path = helpers::get_program_path("vspipe")) {
			vspipe_path = *_vspipe_path;
		}
		else {
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
