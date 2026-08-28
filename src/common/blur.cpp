#include "blur.h"

#include "utils.h"
#include "rendering.h"
#include "updates.h"
#include "config_blur.h"
#include "config_app.h"
#include "config_presets.h"
#include "masks.h"

tl::expected<void, std::string> Blur::initialise(bool _verbose, bool _using_preview) {
	resources_path = u::get_resources_path();
	settings_path = u::get_settings_path();

	auto global_blur_config_path = config_blur::get_global_config_path();
	if (!std::filesystem::exists(global_blur_config_path))
		config_blur::create(global_blur_config_path, config_blur::DEFAULT_CONFIG);

	auto app_config_path = config_app::get_app_config_path();
	if (!std::filesystem::exists(app_config_path))
		config_app::create(app_config_path, GlobalAppSettings{});

	auto preset_config_path = config_presets::get_preset_config_path();
	if (!std::filesystem::exists(preset_config_path))
		config_presets::create(preset_config_path, PresetSettings{});

	// so there's somewhere to drop mask images even before one's been used
	std::error_code masks_ec;
	std::filesystem::create_directories(masks::get_path(), masks_ec);

#if defined(_WIN32)
	used_installer = std::filesystem::exists(resources_path / "lib\\vapoursynth\\vspipe.exe") &&
	                 std::filesystem::exists(resources_path / "lib\\ffmpeg\\ffmpeg.exe");
#elif defined(__linux__)
	// todo
	used_installer = false;
#elif defined(__APPLE__)
	used_installer = std::filesystem::exists(resources_path / "vapoursynth/vspipe") &&
	                 std::filesystem::exists(resources_path / "ffmpeg/ffmpeg");
#endif

	if (used_installer) {
#if defined(_WIN32)
		vspipe_path = (blur.resources_path / "lib\\vapoursynth\\vspipe.exe");
		ffmpeg_path = (blur.resources_path / "lib\\ffmpeg\\ffmpeg.exe");
		ffprobe_path = (blur.resources_path / "lib\\ffmpeg\\ffprobe.exe");
#elif defined(__linux__)
		// todo
#elif defined(__APPLE__)
		vspipe_path = (blur.resources_path / "vapoursynth/vspipe");
		ffmpeg_path = (blur.resources_path / "ffmpeg/ffmpeg");
		ffprobe_path = (blur.resources_path / "ffmpeg/ffprobe");
#endif

		const static std::string manual_troubleshooting_info = "Try redownloading the latest installer.";

		// didn't use installer, check if dependencies are installed
		if (!std::filesystem::exists(ffmpeg_path)) {
			return tl::unexpected("FFmpeg could not be found. " + manual_troubleshooting_info);
		}

		if (!std::filesystem::exists(ffprobe_path)) {
			return tl::unexpected("FFprobe could not be found. " + manual_troubleshooting_info);
		}

		if (!std::filesystem::exists(vspipe_path)) {
			return tl::unexpected("VapourSynth could not be found. " + manual_troubleshooting_info);
		}
	}
	else {
		const static std::string manual_troubleshooting_info =
			"If you're not sure what that means, try using the installer.";

		// didn't use installer, check if dependencies are installed
		if (auto _ffmpeg_path = u::get_program_path("ffmpeg")) {
			ffmpeg_path = *_ffmpeg_path;
		}
		else {
			return tl::unexpected("FFmpeg could not be found. " + manual_troubleshooting_info);
		}

		if (auto _ffprobe_path = u::get_program_path("ffprobe")) {
			ffprobe_path = *_ffprobe_path;
		}
		else {
			return tl::unexpected("FFprobe could not be found. " + manual_troubleshooting_info);
		}

		if (auto _vspipe_path = u::get_program_path("vspipe")) {
			vspipe_path = *_vspipe_path;
		}
		else {
			return tl::unexpected("VapourSynth could not be found. " + manual_troubleshooting_info);
		}
	}

	verbose = _verbose;
	using_preview = _using_preview;

	setup_signal_handlers();

	int atexit_res = std::atexit([] {
		blur.in_atexit = true; // spdlog's already shut down or smth. Cancer
		blur.cleanup();
	});

	if (atexit_res != 0)
		DEBUG_LOG("failed to register atexit");

	initialised = true;

	std::thread([this] {
		initialise_device_lists();
	}).detach();

	return {};
}

void Blur::cleanup() {
	// prevent multiple cleanup calls
	if (cleanup_performed.exchange(true))
		return;

	u::log("Starting application cleanup...");

	exiting = true;

	// stop renders & wait for them to finish stopping
	rendering::video_render_queue.stop_and_wait();

	// remove temp dirs
	DEBUG_LOG("removing temp path {}", temp_path);
	std::filesystem::remove_all(temp_path); // todo: is this unsafe lol

	u::log("Application cleanup completed");
}

tl::expected<updates::UpdateCheckRes, std::string> Blur::check_updates() {
	auto config = config_app::get_app_config();
	if (!config.check_updates)
		return updates::UpdateCheckRes{};

	return updates::is_latest_version(config.check_beta);
}

bool Blur::update(
	const std::string& tag,
	const std::optional<updates::ProgressCallback>& progress_callback,
	const std::optional<updates::CancelCallback>& cancel_callback
) {
	return updates::update_to_tag(tag, progress_callback, cancel_callback);
}

void Blur::initialise_device_lists() {
	rife_devices = u::get_devices("rife");

	std::ranges::copy(
		std::ranges::transform_view(
			rife_devices,
			[](const auto& pair) {
				return pair.second;
			}
		),
		std::back_inserter(rife_device_names)
	);

#ifdef TENSORRT
	tensorrt_devices = u::get_devices("tensorrt");

	std::ranges::copy(
		std::ranges::transform_view(
			tensorrt_devices,
			[](const auto& pair) {
				return pair.second;
			}
		),
		std::back_inserter(tensorrt_device_names)
	);
#endif

	initialised_devices = true;
}

void cleanup_handler(int signal) {
	// Restore default handler immediately to prevent re-entry
	std::signal(signal, SIG_DFL);

	blur.cleanup();

	// Re-raise the signal for proper exit code
	std::raise(signal);
}

void Blur::setup_signal_handlers() {
	std::signal(SIGINT, cleanup_handler);
	std::signal(SIGTERM, cleanup_handler);
#ifndef _WIN32
	std::signal(SIGHUP, cleanup_handler);
#endif
}
