#include "rendering.h"
#include "config_presets.h"
#include "utils.h"

#ifdef __linux__
#	include "config_app.h"
#endif

bool Rendering::render_next_video() {
	if (m_queue.empty())
		return false;

	auto& render = m_queue.front();
	auto* render_ptr = render.get();

	lock();
	{
		m_current_render_id = render->get_render_id();
	}
	unlock();

	rendering.call_progress_callback();

	tl::expected<RenderResult, std::string> render_result;
	try {
		render_result = render->render();
	}
	catch (const std::exception& e) {
		u::log("Render exception: {}", e.what());
	}

	rendering.call_render_finished_callback(
		render_ptr, render_result
	); // note: cant do render.get() here cause compiler optimisations break it somehow (So lit)

	// finished rendering, delete
	lock();
	{
		m_queue.erase(m_queue.begin());
		m_current_render_id.reset();
	}
	unlock();

	rendering.call_progress_callback();

	return true;
}

Render& Rendering::queue_render(Render&& render) {
	lock();
	auto& added = *m_queue.emplace_back(std::make_unique<Render>(std::move(render)));
	unlock();

	return added;
}

void Render::build_output_filename() {
	auto output_folder = this->m_video_folder / this->m_app_settings.output_prefix;
	std::filesystem::create_directories(output_folder);

	// build output filename
	int num = 1;
	do {
		std::string output_filename = this->m_video_name + " - blur";

		if (this->m_settings.detailed_filenames) {
			std::string extra_details;

			// stupid
			if (this->m_settings.blur) {
				if (this->m_settings.interpolate) {
					extra_details = std::format(
						"{}fps ({}, {})",
						this->m_settings.blur_output_fps,
						this->m_settings.interpolated_fps,
						this->m_settings.blur_amount
					);
				}
				else {
					extra_details =
						std::format("{}fps ({})", this->m_settings.blur_output_fps, this->m_settings.blur_amount);
				}
			}
			else {
				if (this->m_settings.interpolate) {
					extra_details = std::format("{}fps", this->m_settings.interpolated_fps);
				}
			}

			if (extra_details != "") {
				output_filename += " ~ " + extra_details;
			}
		}

		if (num > 1)
			output_filename += std::format(" ({})", num);

		output_filename += "." + this->m_settings.advanced.video_container;

		this->m_output_path = output_folder / output_filename;

		num++;
	}
	while (std::filesystem::exists(this->m_output_path));
}

Render::Render(
	std::filesystem::path input_path,
	u::VideoInfo video_info,
	const std::optional<std::filesystem::path>& output_path,
	const std::optional<std::filesystem::path>& config_path
)
	: m_video_path(std::move(input_path)), m_video_info(std::move(video_info)) {
	// set id note: is this silly? seems elegant but i might be missing some edge case
	static uint32_t current_render_id = 1; // 0 is null
	m_render_id = current_render_id++;

	this->m_video_name = this->m_video_path.stem();
	this->m_video_folder = this->m_video_path.parent_path();

	// parse config file (do it now, not when rendering. nice for batch rendering the same file with different settings)
	auto config_res = config_blur::get_config(
		config_path.has_value() ? output_path.value() : config_blur::get_config_filename(m_video_folder),
		!config_path.has_value() // use global only if no config path is specified
	);

	this->m_settings = config_res.config;
	this->m_is_global_config = config_res.is_global;

	this->m_app_settings = config_app::get_app_config();

	if (output_path.has_value())
		this->m_output_path = output_path.value();
	else {
		// note: this uses settings, so has to be called after they're loaded
		build_output_filename();
	}
}

bool Render::create_temp_path() {
	size_t out_hash = std::hash<std::filesystem::path>()(m_output_path);

	auto temp_path = blur.create_temp_path(std::to_string(out_hash));

	if (temp_path)
		m_temp_path = *temp_path;

	return temp_path.has_value();
}

bool Render::remove_temp_path() {
	return Blur::remove_temp_path(m_temp_path);
}

// todo: refactor
tl::expected<RenderCommands, std::string> Render::build_render_commands() {
	RenderCommands commands;

	std::filesystem::path blur_script_path = (blur.resources_path / "lib/blur.py");

	auto settings_json = m_settings.to_json();
	if (!settings_json)
		return tl::unexpected(settings_json.error());

	auto app_settings_json = m_app_settings.to_json();
	if (!app_settings_json)
		return tl::unexpected(app_settings_json.error());

	settings_json->update(*app_settings_json); // adds new keys from app settings (and overrides dupes)

#if defined(__linux__)
	bool vapoursynth_plugins_bundled = std::filesystem::exists(blur.resources_path / "vapoursynth-plugins");
#endif

	// Build vspipe command
	commands.vspipe = { "-p",
		                "-c",
		                "y4m",
		                "-a",
		                "video_path=" + m_video_path.string(),
		                "-a",
		                std::format("fps_num={}", m_video_info.fps_num),
		                "-a",
		                std::format("fps_den={}", m_video_info.fps_den),
		                "-a",
		                "color_range=" + (m_video_info.color_range ? *m_video_info.color_range : "undefined"),
		                "-a",
		                "settings=" + settings_json->dump(),
#if defined(__APPLE__)
		                "-a",
		                std::format("macos_bundled={}", blur.used_installer ? "true" : "false"),
#endif
#if defined(_WIN32)
		                "-a",
		                "enable_lsmash=true",
#endif
#if defined(__linux__)
		                "-a",
		                std::format("linux_bundled={}", vapoursynth_plugins_bundled ? "true" : "false"),
#endif
		                blur_script_path,
		                "-" };

	// Build ffmpeg command
	commands.ffmpeg = { "-loglevel",
		                "error",
		                "-hide_banner",
		                "-stats",
		                "-y",
		                "-i",
		                "-", // piped output from video script
		                "-fflags",
		                "+genpts",
		                "-i",
		                m_video_path.string(), // original video (for audio)
		                "-map",
		                "0:v",
		                "-map",
		                "1:a?" };

	// handle colour metadata tagging
	// (vspipe strips this input info, need to define it manually so ffmpeg knows about it)
	std::vector<std::string> params;

	if (m_video_info.color_range) {
		std::string range = *m_video_info.color_range == "pc" ? "full" : "limited";
		params.emplace_back("range=" + range);
	}

	if (m_video_info.color_space) {
		params.emplace_back("colorspace=" + *m_video_info.color_space);
	}

	if (m_video_info.color_transfer) {
		params.emplace_back("color_trc=" + *m_video_info.color_transfer);
	}

	if (m_video_info.color_primaries) {
		params.emplace_back("color_primaries=" + *m_video_info.color_primaries);
	}

	if (!params.empty()) {
		std::string setparams_filter = "setparams=";
		for (size_t i = 0; i < params.size(); ++i) {
			if (i > 0)
				setparams_filter += ":";
			setparams_filter += params[i];
		}

		commands.ffmpeg.emplace_back("-vf");
		commands.ffmpeg.emplace_back(setparams_filter);
	}

	if (m_video_info.pix_fmt) {
		commands.ffmpeg.emplace_back("-pix_fmt");
		commands.ffmpeg.emplace_back(*m_video_info.pix_fmt);
	}

	// Handle audio filters
	std::vector<std::string> audio_filters;
	if (m_settings.timescale) {
		if (m_settings.input_timescale != 1.f) {
			audio_filters.push_back(
				std::format(
					"asetrate={}*{}",
					m_video_info.sample_rate != -1 ? m_video_info.sample_rate : 48000,
					(1 / m_settings.input_timescale)
				)
			);
			audio_filters.emplace_back("aresample=48000");
		}

		if (m_settings.output_timescale != 1.f) {
			if (m_settings.output_timescale_audio_pitch) {
				audio_filters.push_back(
					std::format(
						"asetrate={}*{}",
						m_video_info.sample_rate != -1 ? m_video_info.sample_rate : 48000,
						m_settings.output_timescale
					)
				);
				audio_filters.emplace_back("aresample=48000");
			}
			else {
				audio_filters.push_back(std::format("atempo={}", m_settings.output_timescale));
			}
		}
	}

	if (!audio_filters.empty()) {
		commands.ffmpeg.emplace_back("-af");
		commands.ffmpeg.push_back(
			std::accumulate(
				std::next(audio_filters.begin()),
				audio_filters.end(),
				audio_filters[0],
				[](const std::string& a, const std::string& b) {
					return a + "," + b;
				}
			)
		);
	}

	if (!m_settings.advanced.ffmpeg_override.empty()) {
		auto args = u::ffmpeg_string_to_args(m_settings.advanced.ffmpeg_override);

		for (const auto& arg : args) {
			commands.ffmpeg.push_back(arg);
		}
	}
	else {
		std::vector<std::string> preset_args = config_presets::get_preset_params(
			m_settings.gpu_encoding ? m_app_settings.gpu_type : "cpu",
			u::to_lower(m_settings.encode_preset.empty() ? "h264" : m_settings.encode_preset),
			m_settings.quality
		);

		commands.ffmpeg.insert(commands.ffmpeg.end(), preset_args.begin(), preset_args.end());

		// audio
		commands.ffmpeg.insert(commands.ffmpeg.end(), { "-c:a", "aac", "-b:a", "320k" });

		// extra
		commands.ffmpeg.insert(commands.ffmpeg.end(), { "-movflags", "+faststart" });
	}

	// Output path
	commands.ffmpeg.push_back(m_output_path.string());

	// Preview output if needed
	if (m_settings.preview && blur.using_preview) {
		commands.ffmpeg.insert(
			commands.ffmpeg.end(),
			{ "-map", "0:v", "-q:v", "2", "-update", "1", "-atomic_writing", "1", "-y", m_preview_path.string() }
		);
	}

	return commands;
}

void Render::update_progress(int current_frame, int total_frames) {
	m_status.current_frame = current_frame;
	m_status.total_frames = total_frames;
	m_status.init_frames = true;

	bool first = !m_status.init_fps;

	if (!m_status.init_fps) {
		m_status.init_fps = true;
		m_status.start_time = std::chrono::steady_clock::now();
		m_status.start_frame = current_frame;
		m_status.fps = 0.f;
	}
	else {
		auto current_time = std::chrono::steady_clock::now();
		m_status.elapsed_time = current_time - m_status.start_time;

		m_status.fps = (m_status.current_frame - m_status.start_frame) / m_status.elapsed_time.count();
	}

	m_status.update_progress_string(first);

	u::log(m_status.progress_string);

	rendering.call_progress_callback();
}

tl::expected<RenderResult, std::string> Render::do_render(RenderCommands render_commands) {
	namespace bp = boost::process;

	m_status = RenderStatus{};
	std::ostringstream vspipe_stderr_output;
	std::ostringstream ffmpeg_stderr_output;

	try {
		bp::pipe vspipe_stdout;
		bp::ipstream vspipe_stderr;
		bp::ipstream ffmpeg_stderr;

#ifndef _DEBUG
		if (m_settings.advanced.debug) {
#endif
			DEBUG_LOG("VSPipe command: {} {}", blur.vspipe_path.string(), u::join(render_commands.vspipe, " "));
			DEBUG_LOG("FFmpeg command: {} {}", blur.ffmpeg_path.string(), u::join(render_commands.ffmpeg, " "));
#ifndef _DEBUG
		}
#endif

		bp::environment env = boost::this_process::environment();

#if defined(__APPLE__)
		if (blur.used_installer) {
			env["PYTHONHOME"] = (blur.resources_path / "python").string();
			env["PYTHONPATH"] = (blur.resources_path / "python/lib/python3.12/site-packages").string();
		}
#endif

#if defined(__linux__)
		auto app_config = config_app::get_app_config();
		if (!app_config.vapoursynth_lib_path.empty()) {
			env["LD_LIBRARY_PATH"] = app_config.vapoursynth_lib_path;
			env["PYTHONPATH"] = app_config.vapoursynth_lib_path + "/python3.12/site-packages";
		}
#endif

		// Launch vspipe process
		bp::child vspipe_process(
			blur.vspipe_path.string(),
			bp::args(render_commands.vspipe),
			bp::std_out > vspipe_stdout,
			bp::std_err > vspipe_stderr,
			env
#ifdef _WIN32
			,
			bp::windows::create_no_window
#endif
		);

		// Launch ffmpeg process
		bp::child ffmpeg_process(
			blur.ffmpeg_path.string(),
			bp::args(render_commands.ffmpeg),
			bp::std_in<vspipe_stdout, bp::std_out.null(), bp::std_err> ffmpeg_stderr,
			env
#ifdef _WIN32
			,
			bp::windows::create_no_window
#endif
		);

		// Store PIDs for signal handler
		m_vspipe_pid = vspipe_process.id();
		m_ffmpeg_pid = ffmpeg_process.id();

		std::thread progress_thread([&]() {
			std::string line;
			std::string progress_line;
			char ch = 0;

			while (ffmpeg_process.running() && vspipe_stderr.get(ch)) {
				if (ch == '\n') {
					// Handle full line for logging
					vspipe_stderr_output << line << '\n';
					line.clear();
				}
				else if (ch == '\r') {
					// Handle progress update
					static std::regex frame_regex(R"(Frame: (\d+)\/(\d+)(?: \((\d+\.\d+) fps\))?)");

					std::smatch match;
					if (std::regex_match(line, match, frame_regex)) {
						int current_frame = std::stoi(match[1]);
						int total_frames = std::stoi(match[2]);

						update_progress(current_frame, total_frames);
					}

					// Don't clear the line for logging purposes
					progress_line = line;
					line.clear();
				}
				else {
					line += ch; // Append character to the line
				}
			}

			// Process any remaining data in the pipe
			std::string remaining;
			while (std::getline(vspipe_stderr, remaining)) {
				vspipe_stderr_output << remaining << '\n';
			}
		});

		std::thread ffmpeg_stderr_thread([&]() {
			std::string line;
			while (std::getline(ffmpeg_stderr, line)) {
				ffmpeg_stderr_output << line << '\n';
			}
		});

		bool killed = false;

		while (vspipe_process.running() || ffmpeg_process.running()) {
			if (m_to_kill) {
				ffmpeg_process.terminate();
				vspipe_process.terminate();
				DEBUG_LOG("render: killed processes early");
				killed = true;
				m_to_kill = false;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		// Clean up
		if (progress_thread.joinable())
			progress_thread.join();

		if (ffmpeg_stderr_thread.joinable())
			ffmpeg_stderr_thread.join();

		m_vspipe_pid = -1;
		m_ffmpeg_pid = -1;

		if (m_settings.advanced.debug)
			u::log(
				"vspipe exit code: {}, ffmpeg exit code: {}", vspipe_process.exit_code(), ffmpeg_process.exit_code()
			);

		if (killed) {
			return RenderResult{
				.stopped = true,
			};
		}

		m_status.finished = true;

		// final progress update
		update_progress(m_status.total_frames, m_status.total_frames);

		std::chrono::duration<float> elapsed_time = std::chrono::steady_clock::now() - m_status.start_time;
		float elapsed_seconds = elapsed_time.count();
		u::log("render finished in {:.2f}s", elapsed_seconds);

		if (vspipe_process.exit_code() != 0 || ffmpeg_process.exit_code() != 0) {
			return tl::unexpected(std::format(
				"--- [vspipe] ---\n{}\n--- [ffmpeg] ---\n{}", vspipe_stderr_output.str(), ffmpeg_stderr_output.str()
			));
		}

		return RenderResult{
			.stopped = false,
		};
	}
	catch (const boost::system::system_error& e) {
		// clean up
		m_vspipe_pid = -1;
		m_ffmpeg_pid = -1;

		u::log_error("Process error: {}", e.what());
		return tl::unexpected(e.what());
	}
}

void Render::pause() {
	if (m_paused)
		return;

	// if (m_vspipe_pid > 0)
	// 	kill(m_vspipe_pid, SIGSTOP);

	if (m_ffmpeg_pid > 0) {
#ifdef WIN32
		u::windows_toggle_suspend_process(m_ffmpeg_pid, true);
#else
		kill(m_ffmpeg_pid, SIGSTOP);
#endif
	}

	m_paused = true;

	m_status.on_pause();

	u::log("Render paused");
}

void Render::resume() {
	if (!m_paused)
		return;

	// if (m_vspipe_pid > 0)
	// 	kill(m_vspipe_pid, SIGCONT);

	if (m_ffmpeg_pid > 0) {
#ifdef WIN32
		u::windows_toggle_suspend_process(m_ffmpeg_pid, false);
#else
		kill(m_ffmpeg_pid, SIGCONT);
#endif
	}

	m_paused = false;

	u::log("Render resumed");
}

tl::expected<RenderResult, std::string> Render::render() {
	if (!blur.initialised)
		return tl::unexpected("Blur not initialised");

	u::log("Rendering '{}'\n", m_video_name);

	if (blur.verbose) {
		u::log("Render settings:");
		u::log("Source video at {:.2f} timescale", m_settings.input_timescale);
		if (m_settings.interpolate)
			u::log(
				"Interpolated to {}fps with {:.2f} timescale", m_settings.interpolated_fps, m_settings.output_timescale
			);
		if (m_settings.blur)
			u::log(
				"Motion blurred to {}fps ({}%)",
				m_settings.blur_output_fps,
				static_cast<int>(m_settings.blur_amount * 100)
			);
		u::log("Rendered at {:.2f} speed with crf {}", m_settings.output_timescale, m_settings.quality);
	}

	// start preview
	if (m_settings.preview && blur.using_preview) {
		if (create_temp_path()) {
			m_preview_path = m_temp_path / "blur_preview.jpg";
		}
	}

	// render
	auto render_commands = build_render_commands();
	if (!render_commands)
		return tl::unexpected(render_commands.error());

	auto render = do_render(*render_commands);
	if (!render) {
		u::log("Failed to render '{}'", m_video_name);

		if (blur.verbose || m_settings.advanced.debug) {
			u::log(render.error());
		}
	}
	else {
		if (render->stopped) {
			u::log("Stopped render '{}'", m_video_name);
			std::filesystem::remove(m_output_path);
		}
		else {
			if (blur.verbose) {
				u::log("Finished rendering '{}'", m_video_name);
			}

			if (m_settings.copy_dates) {
				try {
					auto input_time = std::filesystem::last_write_time(m_video_path);
					std::filesystem::last_write_time(m_output_path, input_time);

					if (m_settings.advanced.debug) {
						u::log("Set output file modified time to match input file");
					}
				}
				catch (const std::exception& e) {
					u::log_error("Failed to set output file timestamp: {}", e.what());
				}
			}
		}
	}

	// stop preview
	remove_temp_path();

	return render;
}

void Rendering::stop_renders_and_wait() {
	auto current_render = get_current_render();
	if (current_render) {
		(*current_render)->stop();
		u::log("Stopping current render");
	}

	// wait for current render to finish
	while (get_current_render_id()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

void RenderStatus::update_progress_string(bool first) {
	float progress = current_frame / (float)total_frames;

	if (first) {
		progress_string = std::format("{:.1f}% complete ({}/{})", progress * 100, current_frame, total_frames);
	}
	else {
		progress_string =
			std::format("{:.1f}% complete ({}/{}, {:.2f} fps)", progress * 100, current_frame, total_frames, fps);
	}
}

void RenderStatus::on_pause() {
	init_fps = false;
	fps = 0.f;
}
