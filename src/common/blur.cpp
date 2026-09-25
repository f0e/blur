#include "blur.h"

#include "utils.h"
#include "rendering.h"
#include "updates.h"
#include "config_base.h"
#include "config_blur.h"
#include "config_app.h"
#include "devices.h"
#include "config_encoding_presets.h"
#include "config_rules.h"
#include "masks.h"
#include "paths.h"
#include "vspipe.h"

tl::expected<void, std::string> Blur::initialise(bool _verbose, bool _using_preview) {
	resources_path = paths::get_resources_path();
	settings_path = paths::get_settings_path();

	// before anything below creates a default in its place
	config_base::migrate_file(
		settings_path / config_app::LEGACY_APP_CONFIG_FILENAME, settings_path / config_app::APP_CONFIG_FILENAME
	);
	config_base::migrate_file(
		settings_path / config_encoding_presets::LEGACY_CONFIG_FILENAME,
		settings_path / config_encoding_presets::CONFIG_FILENAME
	);

	auto app_config_path = config_app::get_app_config_path();
	if (!std::filesystem::exists(app_config_path))
		config_app::create(app_config_path, GlobalAppSettings{});

	auto encoding_preset_config_path = config_encoding_presets::get_config_path();
	if (!std::filesystem::exists(encoding_preset_config_path))
		config_encoding_presets::create(encoding_preset_config_path, EncodingPresetSettings{});

	auto rules_config_path = config_rules::get_config_path();
	if (!std::filesystem::exists(rules_config_path))
		config_rules::create(rules_config_path);

	// after the rules config, which names the default config
	config_blur::initialise_configs();

	// so there's somewhere to drop mask images even before one's been used
	std::error_code masks_ec;
	std::filesystem::create_directories(masks::get_path(), masks_ec);

#if defined(_WIN32)
	used_installer =
		std::filesystem::exists(resources_path / "lib\\vapoursynth\\Lib\\site-packages\\vapoursynth\\vspipe.exe") &&
		std::filesystem::exists(resources_path / "lib\\ffmpeg\\ffmpeg.exe");
#elif defined(__linux__)
	// todo
	used_installer = false;
#elif defined(__APPLE__)
	used_installer =
		std::filesystem::exists(resources_path / "python/lib/python3.12/site-packages/vapoursynth/vspipe") &&
		std::filesystem::exists(resources_path / "ffmpeg/ffmpeg");
#endif

	if (used_installer) {
#if defined(_WIN32)
		vspipe_path = (blur.resources_path / "lib\\vapoursynth\\Lib\\site-packages\\vapoursynth\\vspipe.exe");
		ffmpeg_path = (blur.resources_path / "lib\\ffmpeg\\ffmpeg.exe");
		ffprobe_path = (blur.resources_path / "lib\\ffmpeg\\ffprobe.exe");
#elif defined(__linux__)
		// todo
#elif defined(__APPLE__)
		vspipe_path = (blur.resources_path / "python/lib/python3.12/site-packages/vapoursynth/vspipe");
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

#ifdef __APPLE__
	vspipe::configure();
#endif

	initialised = true;

	std::thread([this] {
		devices::initialise();
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

namespace {
	void exit_handler(int signal) {
		(void)std::signal(signal, SIG_DFL); // let a second signal force quit
		blur.exiting = true;
	}

	void add_handler(int signal) {
		if (std::signal(signal, exit_handler) == SIG_ERR)
			DEBUG_LOG("failed to register handler for signal {}", signal);
	}
}

void Blur::setup_signal_handlers() {
	add_handler(SIGINT);
	add_handler(SIGTERM);
#ifndef _WIN32
	add_handler(SIGHUP);
#endif
}
