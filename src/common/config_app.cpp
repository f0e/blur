#include "config_app.h"
#include "config_base.h"

std::string config_app::generate_config_string(const GlobalAppSettings& settings, bool shareable_only) {
	std::ostringstream output;

	output << "[blur v" << BLUR_VERSION << "]" << "\n";

	output << "\n";
	output << "- pc-specific blur settings" << "\n";
	output << "output prefix: " << settings.output_prefix << "\n";

	if (!shareable_only) {
		output << "gpu type (nvidia/amd/intel): " << settings.gpu_type << "\n";
		output << "rife gpu number: " << settings.rife_device_index << "\n";

#ifdef TENSORRT
		output << "rife (tensorrt) gpu number: " << settings.tensorrt_device_index << "\n";
#endif
	}

	output << "\n";
	output << "- gui" << "\n";

#ifdef BLUR_COLOR_THEMES
	output << "color theme: " << settings.gui_color_hex << "\n";
#endif

	output << "window width: " << settings.gui_width << "\n";
	output << "window height: " << settings.gui_height << "\n";

	output << "dpi scale (0 = auto): " << settings.dpi_scale_override << "\n";
	output << "blur amount tied to fps: " << (settings.blur_amount_tied_to_fps ? "true" : "false") << "\n";
	output << "skip queue: " << (settings.skip_queue ? "true" : "false") << "\n";

	output << "\n";
	output << "- preview" << "\n";
	output << "preview volume: " << settings.preview_volume << "\n";

	if (!shareable_only) {
		output << "hardware accelerated preview: " << (settings.preview_hardware_decoding ? "true" : "false") << "\n";

		output << "sample video path: " << settings.sample_video_path << "\n";
		output << "config preview seek: " << settings.config_preview_seek << "\n";
	}

	output << "\n";
	output << "- desktop notifications" << "\n";
	output << "render success notifications: " << (settings.render_success_notifications ? "true" : "false") << "\n";
	output << "render failure notifications: " << (settings.render_failure_notifications ? "true" : "false") << "\n";
	output << "taskbar progress: " << (settings.taskbar_progress ? "true" : "false") << "\n";

	output << "\n";
	output << "- updates" << "\n";
	output << "check for updates: " << (settings.check_updates ? "true" : "false") << "\n";
	output << "include beta updates: " << (settings.check_beta ? "true" : "false") << "\n";

	if (!shareable_only)
		output << "dismissed update version: " << settings.dismissed_update_version << "\n";

	output << "\n";
	output << "- misc" << "\n";
	output << "notify about config overrides: " << (settings.notify_about_config_override ? "true" : "false") << "\n";

#ifdef __linux__
	if (!shareable_only) {
		output << "\n";
		output << "- linux" << "\n";
		output << "vapoursynth lib path: " << settings.vapoursynth_lib_path << "\n";
	}
#endif

	return output.str();
}

void config_app::create(const std::filesystem::path& filepath, const GlobalAppSettings& settings) {
	config_base::write_config_string(filepath, generate_config_string(settings, false));
}

std::string config_app::export_shareable(const GlobalAppSettings& settings) {
	return generate_config_string(settings, true);
}

void config_app::copy_machine_settings(GlobalAppSettings& to, const GlobalAppSettings& from) {
	to.gpu_type = from.gpu_type;
	to.rife_device_index = from.rife_device_index;
	to.tensorrt_device_index = from.tensorrt_device_index;

	to.preview_hardware_decoding = from.preview_hardware_decoding;

	to.sample_video_path = from.sample_video_path;
	to.config_preview_seek = from.config_preview_seek;

	to.dismissed_update_version = from.dismissed_update_version;

#ifdef __linux__
	to.vapoursynth_lib_path = from.vapoursynth_lib_path;
#endif
}

GlobalAppSettings config_app::parse(const std::string& config_content) {
	std::istringstream stream(config_content);
	auto config_map = config_base::read_config_map(stream);
	return parse_from_map(config_map);
}

GlobalAppSettings config_app::parse(const std::filesystem::path& config_filepath) {
	auto settings = parse(config_base::read_config_file(config_filepath).value_or(""));

	// write formatted file
	create(config_filepath, settings);

	return settings;
}

GlobalAppSettings config_app::parse_from_map(const std::map<std::string, std::string>& config_map) {
	GlobalAppSettings settings;

	config_base::extract_config_value(config_map, "output prefix", settings.output_prefix);
	config_base::extract_config_value(config_map, "gpu type (nvidia/amd/intel)", settings.gpu_type);
	config_base::extract_config_value(config_map, "rife gpu number", settings.rife_device_index);

#ifdef TENSORRT
	config_base::extract_config_value(config_map, "rife (tensorrt) gpu number", settings.tensorrt_device_index);
#endif

#ifdef BLUR_COLOR_THEMES
	config_base::extract_config_value(config_map, "color theme", settings.gui_color_hex);
#endif

	config_base::extract_config_value(config_map, "window width", settings.gui_width);
	config_base::extract_config_value(config_map, "window height", settings.gui_height);
	config_base::extract_config_value(config_map, "dpi scale (0 = auto)", settings.dpi_scale_override);
	config_base::extract_config_value(config_map, "blur amount tied to fps", settings.blur_amount_tied_to_fps);
	config_base::extract_config_value(config_map, "skip queue", settings.skip_queue);

	config_base::extract_config_value(config_map, "preview volume", settings.preview_volume);
	config_base::extract_config_value(config_map, "hardware accelerated preview", settings.preview_hardware_decoding);
	config_base::extract_config_value(config_map, "sample video path", settings.sample_video_path);
	config_base::extract_config_value(config_map, "config preview seek", settings.config_preview_seek);

	settings.config_preview_seek = std::clamp(settings.config_preview_seek, 0.f, 1.f);

	config_base::extract_config_value(
		config_map, "render success notifications", settings.render_success_notifications
	);
	config_base::extract_config_value(
		config_map, "render failure notifications", settings.render_failure_notifications
	);
	config_base::extract_config_value(config_map, "taskbar progress", settings.taskbar_progress);

	config_base::extract_config_value(config_map, "check for updates", settings.check_updates);
	config_base::extract_config_value(config_map, "include beta updates", settings.check_beta);
	config_base::extract_config_value(config_map, "dismissed update version", settings.dismissed_update_version);

	config_base::extract_config_value(
		config_map, "notify about config overrides", settings.notify_about_config_override
	);

#ifdef __linux__
	config_base::extract_config_value(config_map, "vapoursynth lib path", settings.vapoursynth_lib_path);
#endif

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
