#pragma once

struct GlobalAppSettings {
	std::string output_prefix;

	std::string gpu_type;
	int rife_device_index = -1;
	int tensorrt_device_index = -1;

	int gui_width = 591;
	int gui_height = 381;
	bool blur_amount_tied_to_fps = true;
	float dpi_scale_override = 0.f; // 0 = auto (use the os content/dpi scale)

	int preview_volume = 70;

	bool render_success_notifications = false;
	bool render_failure_notifications = false;

	bool check_updates = true;
	bool check_beta = false;

	bool notify_about_config_override = true;

	bool skip_queue = false;

#ifdef __linux__
	std::string vapoursynth_lib_path;
#endif

	bool operator==(const GlobalAppSettings& other) const = default;

	[[nodiscard]] tl::expected<nlohmann::json, std::string> to_json() const;
};

namespace config_app {
	inline const GlobalAppSettings DEFAULT_CONFIG;

	const std::string APP_CONFIG_FILENAME = "blur.cfg";

	// inline const std::vector<std::string> CHECK_UPDATES_OPTIONS = { "off", "on", "beta" };

	void create(const std::filesystem::path& filepath, const GlobalAppSettings& current_settings = GlobalAppSettings());
	GlobalAppSettings parse(const std::filesystem::path& config_filepath);
	std::filesystem::path get_app_config_path();
	GlobalAppSettings get_app_config();
}
