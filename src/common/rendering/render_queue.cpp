#include "render_queue.h"
#include "common/config_blur.h"

rendering::QueueAddRes rendering::VideoRenderQueue::add(
	const std::filesystem::path& input_path,
	const u::VideoInfo& video_info,
	const std::optional<std::filesystem::path>& config_path,
	const GlobalAppSettings& app_settings,
	const std::optional<std::filesystem::path>& output_path_override,
	float start,
	float end,
	const std::function<void()>& progress_callback,
	const std::function<void(
		const VideoRenderDetails& render,
		const tl::expected<rendering::RenderResult, std::variant<std::string, RenderError>>& result
	)>& finish_callback
) {
	// parse config file (do it now, not when rendering. nice for batch rendering the same file with different
	// settings)
	auto config_res = config_blur::get_config(
		config_path.has_value() ? config_path.value() : config_blur::get_config_filename(input_path.parent_path()),
		!config_path.has_value() // use global only if no config path is specified
	);

	// check if preset is valid
	auto valid_presets = u::get_supported_presets(config_res.config.gpu_encoding, app_settings.gpu_type);
	if (!u::contains(valid_presets, config_res.config.encode_preset)) {
		return {
			.is_global_config = config_res.is_global,
			.error = std::format("preset '{}' is not valid", config_res.config.encode_preset),
		};
	}

	std::lock_guard lock(m_mutex);
	auto added = m_queue.emplace_back(
		VideoRenderDetails{
			.input_path = input_path,
			.video_info = video_info,
			.settings = config_res.config,
			.app_settings = app_settings,
			.output_path_override = output_path_override,
			.start = start,
			.end = end,
			.progress_callback = progress_callback,
			.finish_callback = finish_callback,
		}
	);

	return {
		.is_global_config = config_res.is_global,
		.state = added.state,
	};
}
