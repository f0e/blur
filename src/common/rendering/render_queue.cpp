#include "render_queue.h"
#include "render.h"
#include "render_commands.h"
#include "common/config_blur.h"
#include "common/encoding.h"

bool rendering::VideoRenderQueue::process_next() {
	// set before checking m_active so stop_and_wait either sees this render or stops it from starting
	m_processing = true;
	bool processed = m_active && process_front();
	m_processing = false;

	return processed;
}

bool rendering::VideoRenderQueue::process_front() {
	if (m_queue.empty())
		return false;

	auto cur = m_queue.front();

	if (cur.state->wants_stop()) {
		if (cur.finish_callback)
			cur.finish_callback(cur, RenderResult{ .stopped = true });

		std::unique_lock lock(m_mutex);
		m_queue.erase(m_queue.begin());

		return true;
	}

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

bool rendering::VideoRenderQueue::cancel(const std::shared_ptr<RenderState>& state) {
	std::optional<VideoRenderDetails> cancelled;

	{
		std::lock_guard lock(m_mutex);

		auto it = std::ranges::find_if(m_queue, [&](const auto& render) {
			return render.state == state;
		});

		if (it == m_queue.end())
			return false;

		it->state->stop();

		// process_next is holding onto the front render, so leave taking it off the queue to it
		if (it == m_queue.begin())
			return true;

		cancelled = *it;
		m_queue.erase(it);
	}

	if (cancelled->finish_callback)
		cancelled->finish_callback(*cancelled, RenderResult{ .stopped = true });

	return true;
}

void rendering::VideoRenderQueue::stop_and_wait() {
	stop();

	if (auto cur = front())
		cur->state->stop();

	while (m_processing)
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

rendering::QueueAddRes rendering::VideoRenderQueue::add(
	const std::filesystem::path& input_path,
	const media::VideoInfo& video_info,
	const std::optional<std::filesystem::path>& config_path,
	const GlobalAppSettings& app_settings,
	const std::optional<std::filesystem::path>& output_path_override,
	float start,
	float end,
	const std::function<void()>& progress_callback,
	const std::function<void(
		const VideoRenderDetails& render,
		const tl::expected<rendering::RenderResult, std::variant<std::string, RenderError>>& result
	)>& finish_callback,
	const std::optional<std::string>& config_name,
	const std::optional<std::string>& mask_override,
	const std::optional<bool>& auto_mask_override
) {
	// resolve the config now, not when rendering. nice for batch rendering the same file with different
	// settings
	bool named_config = config_name && !config_name->empty();

	// a named config takes priority over a config path
	BlurSettings settings;

	if (config_path && !named_config) {
		settings = config_blur::parse(*config_path);
	}
	else {
		auto resolved_name = config_blur::resolve_config_name(input_path, config_name);

		if (resolved_name.empty()) {
			return {
				.error = "no config selected, no rule matched, and there's no default config set",
			};
		}

		settings = config_blur::get_config(resolved_name);
	}

	if (mask_override)
		settings.mask = *mask_override;

	if (auto_mask_override)
		settings.auto_mask = *auto_mask_override;

	if (!video_info.audio_sample_rates.empty() && detail::copies_audio(settings, app_settings)) {
		if (auto conflict = detail::get_audio_copy_conflict(settings, start != 0.f || end != 1.f)) {
			return {
				.error = conflict,
			};
		}
	}

	// check if preset is valid
	auto valid_presets = encoding::get_supported_encoding_presets(settings.gpu_encoding, app_settings.gpu_type);
	if (!u::contains(valid_presets, settings.encode_preset)) {
		return {
			.error = std::format("preset '{}' is not valid", settings.encode_preset),
		};
	}

	std::lock_guard lock(m_mutex);
	auto added = m_queue.emplace_back(
		VideoRenderDetails{
			.input_path = input_path,
			.video_info = video_info,
			.settings = settings,
			.app_settings = app_settings,
			.output_path_override = output_path_override,
			.start = start,
			.end = end,
			.progress_callback = progress_callback,
			.finish_callback = finish_callback,
		}
	);

	return {
		.state = added.state,
	};
}
