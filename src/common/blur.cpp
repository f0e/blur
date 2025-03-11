#include "blur.h"
#include "common/utils.h"
#include "common/rendering.h"

Blur::InitialisationResponse Blur::initialise(bool _verbose, bool _using_preview) {
	resources_path = u::get_resources_path();
	settings_path = u::get_settings_path();

	used_installer = std::filesystem::exists(resources_path / "lib\\vapoursynth\\vspipe.exe") &&
	                 std::filesystem::exists(resources_path / "lib\\ffmpeg\\ffmpeg.exe");

	if (!used_installer) {
		// didn't use installer, check if dependencies are installed
		if (auto _ffmpeg_path = u::get_program_path("ffmpeg")) {
			ffmpeg_path = *_ffmpeg_path;
		}
		else {
			return {
				.success = false,
				.error_message = "FFmpeg could not be found",
			};
		}

		if (auto _vspipe_path = u::get_program_path("vspipe")) {
			vspipe_path = *_vspipe_path;
		}
		else {
			return {
				.success = false,
				.error_message = "VapourSynth could not be found",
			};
		}
	}

	verbose = _verbose;
	using_preview = _using_preview;

	int res = std::atexit([] {
		rendering.stop_rendering();
		blur.cleanup();
	});

	if (res != 0)
		DEBUG_LOG("failed to register atexit");

	auto global_config_path = config::get_global_config_path();
	if (!std::filesystem::exists(global_config_path))
		config::create(global_config_path, DEFAULT_SETTINGS);

	initialise_base_temp_path();

	return {
		.success = true,
	};
}

void Blur::initialise_base_temp_path() {
	temp_path = std::filesystem::temp_directory_path() / APPLICATION_NAME;
	int i = 0;
	while (true) {
		if (std::filesystem::exists(temp_path)) {
			temp_path = std::filesystem::temp_directory_path() / std::format("{}-{}", APPLICATION_NAME, ++i);
			continue;
		}

		std::filesystem::create_directory(temp_path);
		break;
	}
}

void Blur::cleanup() const {
	u::log("removing temp path {}", temp_path.string());
	std::filesystem::remove_all(temp_path); // todo: is this unsafe lol
}

std::optional<std::filesystem::path> Blur::create_temp_path(const std::string& folder_name) const {
	auto temp_dir = temp_path / folder_name;

	if (std::filesystem::exists(temp_dir)) {
		u::log("temp dir {} already exists, clearing and re-creating", temp_path.string());
		remove_temp_path(temp_dir);
	}

	u::log("trying to make temp dir {}", temp_dir.string());

	if (!std::filesystem::create_directory(temp_dir))
		return {};

	u::log("created temp dir {}", temp_dir.string());

	return temp_dir;
}

bool Blur::remove_temp_path(const std::filesystem::path& temp_path) {
	if (temp_path.empty())
		return false;

	if (!std::filesystem::exists(temp_path))
		return false;

	try {
		std::filesystem::remove_all(temp_path);
		u::log("removed temp dir {}", temp_path.string());

		return true;
	}
	catch (const std::filesystem::filesystem_error& e) {
		u::log_error("Error removing temp path: {}", e.what());
		return false;
	}
}
