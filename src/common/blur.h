#pragma once

#include "updates.h"
#include "config_blur.h"

const std::string APPLICATION_NAME = "blur";
const std::string BLUR_VERSION = "2.45";

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

	void initialise_base_temp_path();

	[[nodiscard]] std::optional<std::filesystem::path> create_temp_path(const std::string& folder_name) const;
	static bool remove_temp_path(const std::filesystem::path& temp_path);

	static tl::expected<updates::UpdateCheckRes, std::string> check_updates();
	static void update(
		const std::string& tag,
		const std::optional<std::function<void(const std::string& text, bool done)>>& progress_callback = {}
	);

	std::map<int, std::string> rife_gpus;
	std::vector<std::string> rife_gpu_names;
	bool initialised_rife_gpus = false;

	void initialise_rife_gpus();
	void pick_fastest_rife_gpu(BlurSettings& settings);

	void setup_signal_handlers();
};

inline Blur blur;
