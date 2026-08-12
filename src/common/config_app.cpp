#include "config_app.h"
#include "config_base.h"

void config_app::create(const std::filesystem::path& filepath, const GlobalAppSettings& settings) {
	std::ofstream output(filepath);

	output << "[blur v" << BLUR_VERSION << "]" << "\n";

	output << "\n";
	output << "- pc-specific blur settings" << "\n";
	output << "output prefix: " << settings.output_prefix << "\n";
	output << "gpu type (nvidia/amd/intel): " << settings.gpu_type << "\n";
	output << "rife gpu number: " << settings.rife_device_index << "\n";

#ifdef TENSORRT
	output << "rife (tensorrt) gpu number: " << settings.tensorrt_device_index << "\n";
#endif

	output << "\n";
	output << "- gui" << "\n";
	output << "window width: " << settings.gui_width << "\n";
	output << "window height: " << settings.gui_height << "\n";
	output << "blur amount tied to fps: " << (settings.blur_amount_tied_to_fps ? "true" : "false") << "\n";
	output << "skip queue: " << (settings.skip_queue ? "true" : "false") << "\n";

	output << "\n";
	output << "- preview" << "\n";
	output << "preview volume: " << settings.preview_volume << "\n";

	output << "\n";
	output << "- desktop notifications" << "\n";
	output << "render success notifications: " << (settings.render_success_notifications ? "true" : "false") << "\n";
	output << "render failure notifications: " << (settings.render_failure_notifications ? "true" : "false") << "\n";

	output << "\n";
	output << "- updates" << "\n";
	output << "check for updates: " << (settings.check_updates ? "true" : "false") << "\n";
	output << "include beta updates: " << (settings.check_beta ? "true" : "false") << "\n";

	output << "\n";
	output << "- misc" << "\n";
	output << "notify about config overrides: " << (settings.notify_about_config_override ? "true" : "false") << "\n";

#ifdef __linux__
	output << "\n";
	output << "- linux" << "\n";
	output << "vapoursynth lib path: " << settings.vapoursynth_lib_path << "\n";
#endif
}

GlobalAppSettings config_app::parse(const std::filesystem::path& config_filepath) {
	std::ifstream file_stream(config_filepath);
	auto config_map = config_base::read_config_map(file_stream);

	GlobalAppSettings settings;

	config_base::extract_config_value(config_map, "output prefix", settings.output_prefix);
	config_base::extract_config_value(config_map, "gpu type (nvidia/amd/intel)", settings.gpu_type);
	config_base::extract_config_value(config_map, "rife gpu number", settings.rife_device_index);

#ifdef TENSORRT
	config_base::extract_config_value(config_map, "rife (tensorrt) gpu number", settings.tensorrt_device_index);
#endif

	config_base::extract_config_value(config_map, "window width", settings.gui_width);
	config_base::extract_config_value(config_map, "window height", settings.gui_height);
	config_base::extract_config_value(config_map, "blur amount tied to fps", settings.blur_amount_tied_to_fps);
	config_base::extract_config_value(config_map, "skip queue", settings.skip_queue);

	config_base::extract_config_value(config_map, "preview volume", settings.preview_volume);

	config_base::extract_config_value(
		config_map, "render success notifications", settings.render_success_notifications
	);
	config_base::extract_config_value(
		config_map, "render failure notifications", settings.render_failure_notifications
	);

	config_base::extract_config_value(config_map, "check for updates", settings.check_updates);
	config_base::extract_config_value(config_map, "include beta updates", settings.check_beta);

	config_base::extract_config_value(
		config_map, "notify about config overrides", settings.notify_about_config_override
	);

#ifdef __linux__
	config_base::extract_config_value(config_map, "vapoursynth lib path", settings.vapoursynth_lib_path);
#endif

	// recreate the config file using the parsed values (keeps nice formatting)
	create(config_filepath, settings);

	return settings;
}

std::filesystem::path config_app::get_app_config_path() {
	return blur.settings_path / APP_CONFIG_FILENAME;
}

GlobalAppSettings config_app::get_app_config() {
	return config_base::load_config<GlobalAppSettings>(get_app_config_path(), create, parse);
}

tl::expected<nlohmann::json, std::string> GlobalAppSettings::to_json() const {
	nlohmann::json j;

	j["gpu_type"] = this->gpu_type;
	j["rife_device_index"] = this->rife_device_index;
	j["tensorrt_device_index"] = this->tensorrt_device_index;

	return j;
}
