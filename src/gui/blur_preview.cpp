#include "blur_preview.h"
#include "ui/helpers/video.h"

#include "common/rendering/render.h"

namespace {
	// stops a held slider from rebuilding the script every ui frame
	constexpr auto REBUILD_DEBOUNCE = std::chrono::milliseconds(250);

	// mpv opens .vpy files through ffmpeg's vapoursynth demuxer, which needs vsscript
	void load_vsscript() {
		static bool loaded = false;
		if (loaded)
			return;

		loaded = true;

#ifdef _WIN32
		// ffmpeg only searches the app and system folders for vsscript.dll, but uses one that's already loaded
		std::filesystem::path vsscript_path =
			blur.used_installer ? blur.resources_path / "lib/vapoursynth/Lib/site-packages/vapoursynth/vsscript.dll"
								: std::filesystem::path(L"VSScript.dll");

		if (!LoadLibraryW(vsscript_path.c_str()))
			u::log_error("failed to load {} ({})", u::path_to_string(vsscript_path), GetLastError());
#endif
	}

	std::filesystem::path temp_file_path(const std::string& extension) {
		static std::atomic<int> count = 0;

#ifdef _WIN32
		auto pid = GetCurrentProcessId();
#else
		auto pid = getpid();
#endif

		return std::filesystem::temp_directory_path() / std::format("blur-preview-{}-{}.{}", pid, ++count, extension);
	}

	// blur.py's output runs faster or slower than the source with timescale on
	double output_speed(const BlurSettings& settings) {
		if (!settings.timescale)
			return 1.0;

		return static_cast<double>(settings.output_timescale) / settings.input_timescale;
	}

	// where in blur.py's output to seek for a position. with blur on the output's frames are sparse, so it's snapped to
	// the nearest one
	float output_seek_target(const BlurSettings& settings, const media::VideoInfo& video_info, float position) {
		double time = position * video_info.video_duration / output_speed(settings);

		if (!settings.blur)
			return static_cast<float>(time);

		double fps = settings.blur_output_fps;
		return VideoPlayer::frame_seek_target(std::round(time * fps) / fps, fps);
	}

	std::vector<std::pair<std::string, std::string>> load_options(float start) {
		return {
			{ "demuxer-lavf-format", "vapoursynth" },

			// every frame the demuxer reads is a blurred frame being rendered, and one can't be cancelled once it's
			// started, so only read what's shown. without the demuxer thread frames are only read when the decoder
			// asks for them, and the latency hacks stop it asking for frames past the one it shows
			{ "demuxer-lavf-probe-info", "no" },
			{ "demuxer-lavf-analyzeduration", "0" },
			{ "cache", "no" },
			{ "demuxer-readahead-secs", "0" },
			{ "demuxer-thread", "no" },
			{ "video-latency-hacks", "yes" },

			{ "start", std::to_string(start) },
		};
	}
}

BlurPreview::~BlurPreview() {
	// mpv has to let go of the script before it's deleted
	m_player.reset();

	std::error_code ec;
	if (!m_script_path.empty())
		std::filesystem::remove(m_script_path, ec);
	if (!m_log_path.empty())
		std::filesystem::remove(m_log_path, ec);
}

void BlurPreview::update(const Request& request) {
	if (!m_player) {
		load_vsscript();

		m_player = std::make_shared<VideoPlayer>(0.f, false);
		m_script_path = temp_file_path("vpy");
		m_log_path = temp_file_path("log");
	}

	Key key{
		.video_path = request.video_path,
		.settings = request.settings,
		.gpu_type = request.app_settings.gpu_type,
		.rife_device = request.app_settings.rife_device,
		.tensorrt_device = request.app_settings.tensorrt_device,
		.mask = request.mask,
	};

	if (m_requested != key)
		m_failed = false;

	m_requested = key;
	m_requested_position = request.position;

	if (auto error = m_player->take_load_error()) {
		auto parsed = rendering::detail::parse_error_output(*error);
		fail(
			parsed ? *parsed
				   : rendering::RenderError{
						 .user_message = "Failed to load blur.py",
						 .technical_details = *error,
					 }
		);
	}

	if (m_pending && m_pending->script.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		finish_build();

	auto now = std::chrono::steady_clock::now();
	bool loaded_or_loading = m_pending ? m_pending->key == key : m_loaded == key;

	if (!loaded_or_loading && !m_pending && now - m_last_build >= REBUILD_DEBOUNCE) {
		m_last_build = now;
		start_build(request);
	}

	if (m_loaded == key && !m_pending && m_position != request.position) {
		m_position = request.position;
		m_player->seek(output_seek_target(request.settings, request.video_info, request.position), true);
	}

	if (!ready_player())
		read_log();
}

void BlurPreview::start_build(const Request& request) {
	m_pending = PendingScript{
		.key = *m_requested,
		.video_info = request.video_info,
		// device indices can wait on device detection, so it's built off the main thread
		.script = std::async(
			std::launch::async,
			[video_path = request.video_path,
		     settings = request.settings,
		     app_settings = request.app_settings,
		     video_info = request.video_info,
		     mask = request.mask,
		     log_path = m_log_path] {
				return rendering::build_preview_script(video_path, settings, app_settings, video_info, mask, log_path);
			}
		),
	};
}

void BlurPreview::finish_build() {
	auto pending = std::move(*m_pending);
	m_pending.reset();

	auto script = pending.script.get();

	// requested settings changed while it was building
	if (pending.key != m_requested)
		return;

	if (!script) {
		// counts as loaded so the same settings aren't retried
		m_player->stop();
		m_loaded = pending.key;
		fail({ .user_message = script.error() });
		return;
	}

	std::ofstream(m_script_path, std::ios::binary) << *script;
	{
		std::ofstream clear_log(m_log_path, std::ios::trunc);
	}
	m_log_offset = 0;

	m_state = std::make_shared<rendering::RenderState>();
	m_failed = false;

	u::log("loading blur preview for {}", u::path_to_string(pending.key.video_path));

	m_player->load_file(
		m_script_path,
		{},
		load_options(output_seek_target(pending.key.settings, pending.video_info, m_requested_position))
	);

	m_loaded = pending.key;
	m_position = m_requested_position;
}

void BlurPreview::read_log() {
	std::ifstream log(m_log_path);
	if (!log)
		return;

	log.seekg(m_log_offset);

	// a line still being written is left for next time
	std::string line;
	while (std::getline(log, line) && !log.eof()) {
		m_log_offset = log.tellg();

		m_state->report_log_line(line);

		if (!line.empty())
			u::log("blur preview: {}", line);
	}
}

void BlurPreview::fail(rendering::RenderError error) {
	u::log_error("blur preview failed: {}", error.to_string());

	m_error = std::move(error);
	m_failed = true;
}

std::shared_ptr<VideoPlayer> BlurPreview::ready_player() const {
	if (!m_player)
		return nullptr;

	bool current =
		!m_failed && !m_pending && m_requested && m_loaded == m_requested && m_position == m_requested_position;

	if (!current || !m_player->has_frame() || !m_player->get_video_dimensions() || !m_player->seek_settled())
		return nullptr;

	return m_player;
}

std::shared_ptr<VideoPlayer> BlurPreview::previous_player() const {
	if (!m_player || m_failed || !m_player->has_frame() || !m_player->get_video_dimensions())
		return nullptr;

	return m_player;
}

BlurPreview::Status BlurPreview::status() const {
	auto progress = m_state->get_progress();

	return {
		.init_stage = progress.init_stage,
		.frame_timing_log = progress.frame_timing_log,
		.failed = m_failed,
	};
}

double BlurPreview::source_time(const BlurSettings& settings, const media::VideoInfo& video_info, float position) {
	double time = position * video_info.video_duration;

	if (!settings.blur)
		return time;

	double fps = settings.blur_output_fps;
	double output_time = std::round(time / output_speed(settings) * fps) / fps;

	return output_time * output_speed(settings);
}

std::optional<rendering::RenderError> BlurPreview::take_error() {
	return std::exchange(m_error, std::nullopt);
}

void BlurPreview::handle_event(const SDL_Event& event, bool& to_render) {
	if (m_player)
		m_player->handle_mpv_event(event, to_render, true);
}

bool BlurPreview::save_frame(
	const std::filesystem::path& path, std::function<void(std::optional<std::string> error)> on_done
) const {
	auto player = ready_player();
	if (!player)
		return false;

	player->screenshot_to_file(path, std::move(on_done));
	return true;
}
