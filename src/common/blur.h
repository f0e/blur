#pragma once

#include "updates.h"
#include "config_blur.h"

const std::string APPLICATION_NAME = "blur";
const std::string BLUR_VERSION = "2.45";

#ifdef WIN32
#	define TENSORRT
#endif

class Blur { // todo: switch all the classes which could be namespaces into namespaces
public:
	bool initialised = false;
	bool exiting = false;
	std::atomic<bool> cleanup_performed;
	bool in_atexit = false;

	bool verbose = true;
	bool using_preview = false;

	std::filesystem::path temp_path;

	std::filesystem::path resources_path;
	std::filesystem::path settings_path;
	bool used_installer = false;

	std::filesystem::path ffmpeg_path;
	std::filesystem::path ffprobe_path;
	std::filesystem::path vspipe_path;

	tl::expected<void, std::string> initialise(bool _verbose, bool _using_preview);

	void cleanup();

	static tl::expected<updates::UpdateCheckRes, std::string> check_updates();
	static void update(
		const std::string& tag,
		const std::optional<std::function<void(const std::string& text, bool done)>>& progress_callback = {}
	);

	// TODO: this stuff probably shouldn't be here
	std::map<int, std::string> rife_devices;
	std::vector<std::string> rife_device_names;

	std::map<int, std::string> tensorrt_devices;
	std::vector<std::string> tensorrt_device_names;
	bool initialised_devices = false;

	void initialise_device_lists();

	void setup_signal_handlers();
};

inline Blur blur;
