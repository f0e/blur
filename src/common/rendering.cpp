#include "rendering.h"

void Rendering::render_videos() {
	if (!m_queue.empty()) {
		auto& render = m_queue.front();
		auto* render_ptr = render.get();

		lock();
		{
			m_current_render_id = render->get_render_id();
		}
		unlock();

		rendering.call_progress_callback();

		RenderResult render_result;
		try {
			render_result = render->render();
		}
		catch (const std::exception& e) {
			u::log(e.what());
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
	}
	else {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

Render& Rendering::queue_render(Render&& render) {
	lock();
	auto& added = *m_queue.emplace_back(std::make_unique<Render>(std::move(render)));
	unlock();

	return added;
}

void Render::build_output_filename() {
	// build output filename
	int num = 1;
	do {
		std::wstring output_filename = this->m_video_name + L" - blur";

		if (this->m_settings.detailed_filenames) {
			std::wstring extra_details;

			// stupid
			if (this->m_settings.blur) {
				if (this->m_settings.interpolate) {
					extra_details = std::format(
						L"{}fps ({}, {})",
						this->m_settings.blur_output_fps,
						u::towstring(this->m_settings.interpolated_fps),
						this->m_settings.blur_amount
					);
				}
				else {
					extra_details =
						std::format(L"{}fps ({})", this->m_settings.blur_output_fps, this->m_settings.blur_amount);
				}
			}
			else {
				if (this->m_settings.interpolate) {
					extra_details = std::format(L"{}fps", u::towstring(this->m_settings.interpolated_fps));
				}
			}

			if (extra_details != L"") {
				output_filename += L" ~ " + extra_details;
			}
		}

		if (num > 1)
			output_filename += std::format(L" ({})", num);

		output_filename += L"." + u::towstring(this->m_settings.video_container);

		this->m_output_path = this->m_video_folder / output_filename;

		num++;
	}
	while (std::filesystem::exists(this->m_output_path));
}

Render::Render(
	std::filesystem::path input_path,
	const std::optional<std::filesystem::path>& output_path,
	const std::optional<std::filesystem::path>& config_path
)
	: m_video_path(std::move(input_path)) {
	// set id note: is this silly? seems elegant but i might be missing some edge case
	static uint32_t current_render_id = 1; // 0 is null
	m_render_id = current_render_id++;

	this->m_video_name = this->m_video_path.stem().wstring();
	this->m_video_folder = this->m_video_path.parent_path();

	// parse config file (do it now, not when rendering. nice for batch rendering the same file with different settings)
	if (config_path.has_value())
		this->m_settings =
			config_blur::get_config(output_path.value(), false); // specified config path, don't use global
	else
		this->m_settings = config_blur::get_config(config_blur::get_config_filename(m_video_folder), true);

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

// NOLINTBEGIN(readability-function-cognitive-complexity) todo: refactor
RenderCommands Render::build_render_commands() {
	RenderCommands commands;

	if (blur.used_installer) {
#if defined(_WIN32)
		commands.vspipe_path = (blur.resources_path / "lib\\vapoursynth\\vspipe.exe").wstring();
		commands.ffmpeg_path = (blur.resources_path / "lib\\ffmpeg\\ffmpeg.exe").wstring();
#elif defined(__linux__)
		// todo
#elif defined(__APPLE__)
		commands.vspipe_path = (blur.resources_path / "vapoursynth/vspipe").wstring();
		commands.ffmpeg_path = (blur.resources_path / "ffmpeg/ffmpeg").wstring();
#endif
	}
	else {
		commands.vspipe_path = blur.vspipe_path.wstring();
		commands.ffmpeg_path = blur.ffmpeg_path.wstring();
	}

	std::wstring path_string = m_video_path.wstring();
	std::ranges::replace(path_string, '\\', '/');

	std::wstring blur_script_path = (blur.resources_path / "lib/blur.py").wstring();

	// Build vspipe command
	commands.vspipe = { L"-p",
		                L"-c",
		                L"y4m",
		                L"-a",
		                L"video_path=" + path_string,
		                L"-a",
		                L"settings=" + u::towstring(m_settings.to_json().dump()),
#if defined(__APPLE__)
		                L"-a",
		                std::format(L"macos_bundled={}", blur.used_installer ? L"true" : L"false"),
#endif
		                blur_script_path,
		                L"-" };

	// Build ffmpeg command
	commands.ffmpeg = { L"-loglevel",
		                L"error",
		                L"-hide_banner",
		                L"-stats",
		                L"-y",
		                L"-i",
		                L"-", // piped output from video script
		                L"-i",
		                m_video_path.wstring(), // original video (for audio)
		                L"-map",
		                L"0:v",
		                L"-map",
		                L"1:a?" };

	// Handle audio filters
	std::vector<std::wstring> audio_filters;
	if (m_settings.timescale) {
		if (m_settings.input_timescale != 1.f) {
			audio_filters.push_back(std::format(L"asetrate=48000*{}", (1 / m_settings.input_timescale)));
		}

		if (m_settings.output_timescale != 1.f) {
			if (m_settings.output_timescale_audio_pitch) {
				audio_filters.push_back(std::format(L"asetrate=48000*{}", m_settings.output_timescale));
			}
			else {
				audio_filters.push_back(std::format(L"atempo={}", m_settings.output_timescale));
			}
		}
	}

	if (!audio_filters.empty()) {
		commands.ffmpeg.emplace_back(L"-af");
		commands.ffmpeg.push_back(std::accumulate(
			std::next(audio_filters.begin()),
			audio_filters.end(),
			audio_filters[0],
			[](const std::wstring& a, const std::wstring& b) {
				return a + L"," + b;
			}
		));
	}

	if (!m_settings.ffmpeg_override.empty()) {
		// Split override string into individual arguments
		std::wistringstream iss(u::towstring(m_settings.ffmpeg_override));
		std::wstring token;
		while (iss >> token) {
			commands.ffmpeg.push_back(token);
		}
	}
	else {
		// Video format
		if (m_settings.gpu_rendering) {
			std::string gpu_type = u::to_lower(m_settings.gpu_type);
			if (gpu_type == "nvidia") {
				commands.ffmpeg.insert(
					commands.ffmpeg.end(),
					{ L"-c:v", L"h264_nvenc", L"-preset", L"p7", L"-qp", std::to_wstring(m_settings.quality) }
				);
			}
			else if (gpu_type == "amd") {
				commands.ffmpeg.insert(
					commands.ffmpeg.end(),
					{ L"-c:v",
				      L"h264_amf",
				      L"-qp_i",
				      std::to_wstring(m_settings.quality),
				      L"-qp_b",
				      std::to_wstring(m_settings.quality),
				      L"-qp_p",
				      std::to_wstring(m_settings.quality),
				      L"-quality",
				      L"quality" }
				);
			}
			else if (gpu_type == "intel") {
				commands.ffmpeg.insert(
					commands.ffmpeg.end(),
					{ L"-c:v",
				      L"h264_qsv",
				      L"-global_quality",
				      std::to_wstring(m_settings.quality),
				      L"-preset",
				      L"veryslow" }
				);
			}
			// todo: mac
		}
		else {
			commands.ffmpeg.insert(
				commands.ffmpeg.end(),
				{ L"-c:v",
			      L"libx264",
			      L"-pix_fmt",
			      L"yuv420p",
			      L"-preset",
			      L"superfast",
			      L"-crf",
			      std::to_wstring(m_settings.quality) }
			);
		}

		// Audio format
		commands.ffmpeg.insert(
			commands.ffmpeg.end(), { L"-c:a", L"aac", L"-b:a", L"320k", L"-movflags", L"+faststart" }
		);
	}

	// Output path
	commands.ffmpeg.push_back(m_output_path.wstring());

	// Preview output if needed
	if (m_settings.preview && blur.using_preview) {
		commands.ffmpeg.insert(
			commands.ffmpeg.end(),
			{ L"-map",
		      L"0:v",
		      L"-q:v",
		      L"2",
		      L"-update",
		      L"1",
		      L"-atomic_writing",
		      L"1",
		      L"-y",
		      m_preview_path.wstring() }
		);
	}

	return commands;
}

// NOLINTEND(readability-function-cognitive-complexity)

void Render::update_progress(int current_frame, int total_frames) {
	m_status.current_frame = current_frame;
	m_status.total_frames = total_frames;

	bool first = !m_status.init;

	if (!m_status.init) {
		m_status.init = true;
		m_status.start_time = std::chrono::steady_clock::now();
	}
	else {
		auto current_time = std::chrono::steady_clock::now();
		m_status.elapsed_time = current_time - m_status.start_time;

		m_status.fps = m_status.current_frame / m_status.elapsed_time.count();
	}

	m_status.update_progress_string(first);

	u::log(m_status.progress_string);

	rendering.call_progress_callback();
}

RenderResult Render::do_render(RenderCommands render_commands) {
	namespace bp = boost::process;

	m_status = RenderStatus{};
	std::ostringstream vspipe_stderr_output;

	try {
		boost::asio::io_context io_context;
		bp::pipe vspipe_stdout;
		bp::ipstream vspipe_stderr;

		if (m_settings.debug) {
			u::log(L"VSPipe command: {} {}", render_commands.vspipe_path, u::join(render_commands.vspipe, L" "));
			u::log(L"FFmpeg command: {} {}", render_commands.ffmpeg_path, u::join(render_commands.ffmpeg, L" "));
		}

		bp::environment env = boost::this_process::environment();

#if defined(__APPLE__)
		if (blur.used_installer) {
			env["PYTHONHOME"] = (blur.resources_path / "python").string();
			env["PYTHONPATH"] = (blur.resources_path / "python/lib/python3.12/site-packages").string();
		}
#endif

		// Launch vspipe process
		bp::child vspipe_process(
			render_commands.vspipe_path,
			bp::args(render_commands.vspipe),
			bp::std_out > vspipe_stdout,
			bp::std_err > vspipe_stderr,
			env,
			io_context
#ifdef _WIN32
			,
			bp::windows::create_no_window
#endif
		);

		// Launch ffmpeg process
		bp::child ffmpeg_process(
			render_commands.ffmpeg_path,
			bp::args(render_commands.ffmpeg),
			bp::std_in < vspipe_stdout,
			bp::std_out.null(),
			// bp::std_err.null(),
			io_context
#ifdef _WIN32
			,
			bp::windows::create_no_window
#endif
		);

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

		vspipe_process.detach();
		ffmpeg_process.detach();

		bool killed = false;

		while (vspipe_process.running() || ffmpeg_process.running()) {
			if (m_to_kill) {
				ffmpeg_process.terminate();
				vspipe_process.terminate();
				u::log("render: killed processes early");
				killed = true;
				m_to_kill = false;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		// Clean up
		if (progress_thread.joinable())
			progress_thread.join();

		if (m_settings.debug)
			u::log(
				"vspipe exit code: {}, ffmpeg exit code: {}", vspipe_process.exit_code(), ffmpeg_process.exit_code()
			);

		if (killed) {
			return {
				.stopped = true,
			};
		}

		m_status.finished = true;
		bool success = vspipe_process.exit_code() == 0 && ffmpeg_process.exit_code() == 0;
		// Final progress update
		if (success)
			update_progress(m_status.total_frames, m_status.total_frames);

		std::chrono::duration<float> elapsed_time = std::chrono::steady_clock::now() - m_status.start_time;
		float elapsed_seconds = elapsed_time.count();
		u::log("render finished in {:.2f}s", elapsed_seconds);

		return {
			.success = success,
			.error_message = vspipe_stderr_output.str(),
		};
	}
	catch (const boost::system::system_error& e) {
		u::log_error("Process error: {}", e.what());

		return {
			.success = false,
			.error_message = e.what(),
		};
	}
}

RenderResult Render::render() {
	if (!blur.initialised)
		return {
			.success = false,
			.error_message = "Blur not initialised",
		};

	u::log(L"Rendering '{}'\n", m_video_name);

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
	RenderCommands render_commands = build_render_commands();

	auto render_res = do_render(render_commands);

	if (render_res.stopped) {
		u::log(L"Stopped render '{}'", m_video_name);
		std::filesystem::remove(m_output_path);
	}
	else if (render_res.success) {
		if (blur.verbose) {
			u::log(L"Finished rendering '{}'", m_video_name);
		}

		if (m_settings.copy_dates) {
			try {
				auto inputTime = std::filesystem::last_write_time(m_video_path);
				std::filesystem::last_write_time(m_output_path, inputTime);

				if (m_settings.debug) {
					u::log(L"Set output file modified time to match input file");
				}
			}
			catch (const std::exception& e) {
				u::log_error("Failed to set output file timestamp: {}", e.what());
			}
		}
	}
	else {
		u::log(L"Failed to render '{}'", m_video_name);

		if (blur.verbose || m_settings.debug) {
			u::log(render_res.error_message);
		}
	}

	// stop preview
	remove_temp_path();

	return render_res;
}

void Rendering::stop_rendering() {
	for (auto& render : m_queue) {
		render->stop();
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
