#include "render_queue.h"
#include "render.h"
#include "render_commands.h"
#include "common/config_blur.h"

bool rendering::VideoRenderQueue::process_next() {
	if (m_queue.empty() || !m_active)
		return false;

	auto cur = m_queue.front();

	auto res = detail::render_video(
		cur.input_path,
		cur.video_info,
		cur.settings,
		cur.state,
		cur.app_settings,
		cur.output_path_override,
		cur.start,
		cur.end,
		cur.progress_callback
	);

	if (cur.finish_callback)
		cur.finish_callback(cur, res);

	std::unique_lock lock(m_mutex);
	m_queue.erase(m_queue.begin());

	return true;
}

void rendering::VideoRenderQueue::stop_and_wait() {
	stop();

	std::lock_guard lock(m_mutex);
	if (m_queue.empty())
		return;

	// still rendering the video at the front, so tell it to stop
	auto cur = m_queue.front();
	cur.state->stop();

	while (!is_empty()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

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

	if (!video_info.audio_sample_rates.empty() && detail::copies_audio(config_res.config, app_settings)) {
		if (auto conflict = detail::get_audio_copy_conflict(config_res.config, start != 0.f || end != 1.f)) {
			return {
				.is_global_config = config_res.is_global,
				.error = *conflict,
			};
		}
	}

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
