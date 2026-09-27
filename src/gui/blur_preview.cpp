#include "blur_preview.h"
#include "ui/helpers/video.h"

#include "common/rendering/render.h"

namespace {
	// stops a held slider from rebuilding the script every ui frame
	constexpr auto REBUILD_DEBOUNCE = std::chrono::milliseconds(250);

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

	double output_speed(const BlurSettings& settings) {
		if (!settings.timescale)
			return 1.0;

		return static_cast<double>(settings.output_timescale) / settings.input_timescale;
	}

	// the time in blur.py's output of the frame shown for a position. with blur on the output's frames are sparse,
	// so it's the nearest one
	double output_time(const BlurSettings& settings, const media::VideoInfo& video_info, float position) {
		double time = position * video_info.video_duration / output_speed(settings);

		if (!settings.blur)
			return time;

		double fps = settings.blur_output_fps;
		return std::round(time * fps) / fps;
	}

	float output_seek_target(const BlurSettings& settings, const media::VideoInfo& video_info, float position) {
		double time = output_time(settings, video_info, position);

		if (!settings.blur)
			return static_cast<float>(time);

		return VideoPlayer::frame_seek_target(time, settings.blur_output_fps);
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

	constexpr std::string_view TEMP_PREFIX = "blur-preview-";

	unsigned long current_pid() {
#ifdef _WIN32
		return GetCurrentProcessId();
#else
		return getpid();
#endif
	}

	bool process_running(unsigned long pid) {
#ifdef _WIN32
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

		// it's there, it just isn't ours to look at
		if (!process)
			return GetLastError() == ERROR_ACCESS_DENIED;

		DWORD exit_code = 0;
		bool running = GetExitCodeProcess(process, &exit_code) != 0 && exit_code == STILL_ACTIVE;
		CloseHandle(process);
		return running;
#else
		return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
	}

	std::filesystem::path temp_folder() {
		return std::filesystem::temp_directory_path() / std::format("{}{}", TEMP_PREFIX, current_pid());
	}
}

std::filesystem::path BlurPreview::temp_file_path(const std::string& extension) {
	static std::atomic<int> count = 0;

	auto folder = temp_folder();

	std::error_code ec;
	std::filesystem::create_directories(folder, ec);

	return folder / std::format("{}.{}", ++count, extension);
}

void BlurPreview::remove_stale_temp_files() {
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path(), ec)) {
		auto name = u::path_to_string(entry.path().filename());
		if (!name.starts_with(TEMP_PREFIX))
			continue;

		// folders are named for the process they're from. older versions left loose files, named the same way
		unsigned long pid = 0;
		auto digits = std::string_view(name).substr(TEMP_PREFIX.size());
		auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), pid);
		if (parsed.ec != std::errc() || pid == current_pid() || process_running(pid))
			continue;

		std::error_code remove_error;
		std::filesystem::remove_all(entry.path(), remove_error);
	}
}

void BlurPreview::remove_temp_files() {
	std::error_code ec;
	std::filesystem::remove_all(temp_folder(), ec);

	if (ec)
		u::log_error("failed to remove preview files: {}", ec.message());
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
	// compared as the output frame's time, since nearby positions can land on the same frame
	m_requested_target = output_seek_target(request.settings, request.video_info, request.position);

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

	if (m_loaded == key && !m_pending && m_target != m_requested_target) {
		m_target = m_requested_target;
		m_player->seek(m_requested_target, true);
	}

	if (!ready_player())
		read_log();
}

void BlurPreview::start_build(const Request& request) {
	m_pending = PendingScript{
		.key = *m_requested,
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

	m_state = std::make_unique<rendering::RenderState>();
	m_failed = false;

	u::log("loading blur preview for {}", u::path_to_string(pending.key.video_path));

	m_player->load_file(m_script_path, {}, load_options(m_requested_target));

	m_loaded = pending.key;
	m_target = m_requested_target;
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

	bool current = !m_failed && !m_pending && m_requested && m_loaded == m_requested && m_target == m_requested_target;

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
	return output_time(settings, video_info, position) * output_speed(settings);
}

std::optional<rendering::RenderError> BlurPreview::take_error() {
	return std::exchange(m_error, std::nullopt);
}

void BlurPreview::handle_event(const SDL_Event& event, bool& to_render) {
	if (m_player)
		m_player->handle_mpv_event(event, to_render, true);
}

std::optional<std::string> PreviewState::status_text(std::optional<std::string> loading_text) const {
	if (sample_status)
		return sample_status;

	if (playing)
		return "pause to see blur's output, or shift+space to play a blurred sample";

	if (frame)
		return std::nullopt;

	if (status.failed)
		return "couldn't generate the preview";

	switch (status.init_stage) {
		case rendering::RenderState::InitStage::GENERATING_MASK:
			return "analysing video to generate a mask...";
		case rendering::RenderState::InitStage::BUILDING_ENGINE:
			return "building tensorrt engine, this may take a few minutes...";
		case rendering::RenderState::InitStage::NONE:
			break;
	}

	return loading_text;
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
