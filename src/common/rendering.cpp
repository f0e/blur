#include "rendering.h"

#include <algorithm>

#ifdef BLUR_GUI
#	include <gui/console.h>
#endif

void Rendering::render_videos() {
	if (!m_queue.empty()) {
		lock();
		{
			m_current_render = m_queue.front().get();
		}
		unlock();

		run_callbacks(); // todo: are these redundant

		try {
			m_current_render->render();
		}
		catch (const std::exception& e) {
			u::log(e.what());
		}

		// finished rendering, delete
		lock();
		{
			m_queue.erase(m_queue.begin());
			m_current_render = nullptr;
		}
		unlock();

		run_callbacks(); // todo: are these redundant
	}
	else {
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}
}

Render& Rendering::queue_render(Render&& render) {
	m_queue.push_back(std::make_unique<Render>(std::move(render)));
	return *m_queue.back();
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
	static uint32_t current_render_id = 0;
	m_render_id = current_render_id++;

	this->m_video_name = this->m_video_path.stem().wstring();
	this->m_video_folder = this->m_video_path.parent_path();

	// parse config file (do it now, not when rendering. nice for batch rendering the same file with different settings)
	if (config_path.has_value())
		this->m_settings = config::get_config(output_path.value(), false); // specified config path, don't use global
	else
		this->m_settings = config::get_config(config::get_config_filename(m_video_folder), true);

	if (output_path.has_value())
		this->m_output_path = output_path.value();
	else {
		// note: this uses settings, so has to be called after they're loaded
		build_output_filename();
	}
}

bool Render::create_temp_path() {
	size_t out_hash = std::hash<std::filesystem::path>()(m_output_path);

	m_temp_path = std::filesystem::temp_directory_path() / std::string("blur-" + std::to_string(out_hash));

	if (!std::filesystem::create_directory(m_temp_path))
		return false;

	blur.created_temp_paths.insert(m_temp_path);

	return true;
}

bool Render::remove_temp_path() {
	if (m_temp_path.empty())
		return false;

	if (!std::filesystem::exists(m_temp_path))
		return false;

	try {
		std::filesystem::remove_all(m_temp_path);
		blur.created_temp_paths.erase(m_temp_path);

		return true;
	}
	catch (const std::filesystem::filesystem_error& e) {
		u::log_error("Error removing temp path: {}", e.what());
		return false;
	}
}

Render::RenderCommands Render::build_render_commands() {
	RenderCommands commands;

	if (blur.used_installer) {
		commands.vspipe_path = (blur.path / "lib\\vapoursynth\\vspipe.exe").wstring();
		commands.ffmpeg_path = (blur.path / "lib\\ffmpeg\\ffmpeg.exe").wstring();
	}
	else {
		commands.vspipe_path = blur.vspipe_path.wstring();
		commands.ffmpeg_path = blur.ffmpeg_path.wstring();
	}

	std::wstring path_string = m_video_path.wstring();
	std::ranges::replace(path_string, '\\', '/');

	std::wstring blur_script_path = (blur.path / "lib/blur.py").wstring();

	// Build vspipe command
	commands.vspipe = { L"-p",
		                L"-c",
		                L"y4m",
		                L"-a",
		                L"video_path=" + path_string,
		                L"-a",
		                L"settings=" + u::towstring(m_settings.to_json().dump()),
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

	rendering.run_callbacks();
}

bool Render::do_render(RenderCommands render_commands) {
	namespace bp = boost::process;

	m_status = RenderStatus{};

	try {
		boost::asio::io_context io_context;
		bp::pipe vspipe_stdout;
		bp::ipstream vspipe_stderr;

		if (m_settings.debug) {
			u::log(L"VSPipe command: {} {}", render_commands.vspipe_path, u::join(render_commands.vspipe, L" "));
			u::log(L"FFmpeg command: {} {}", render_commands.ffmpeg_path, u::join(render_commands.ffmpeg, L" "));
		}

		// Launch vspipe process
		bp::child vspipe_process(
			render_commands.vspipe_path,
			bp::args(render_commands.vspipe),
			bp::std_out > vspipe_stdout,
			bp::std_err > vspipe_stderr,
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

		std::thread read_thread([&]() {
			std::string line;
			char ch = 0;

			while (ffmpeg_process.running() && vspipe_stderr.get(ch)) {
				if (ch == '\r') {
					static std::regex frame_regex(R"(Frame: (\d+)\/(\d+)(?: \((\d+\.\d+) fps\))?)");

					std::smatch match;
					if (std::regex_match(line, match, frame_regex)) {
						int current_frame = std::stoi(match[1]);
						int total_frames = std::stoi(match[2]);

						update_progress(current_frame, total_frames);
					}

					line.clear();
				}
				else {
					line += ch; // Append character to the line
				}
			}
		});

		vspipe_process.wait();
		ffmpeg_process.wait();

		// Clean up
		if (read_thread.joinable()) {
			read_thread.join();
		}

		if (m_settings.debug)
			u::log(
				"vspipe exit code: {}, ffmpeg exit code: {}", vspipe_process.exit_code(), ffmpeg_process.exit_code()
			);

		bool success = vspipe_process.exit_code() == 0 && ffmpeg_process.exit_code() == 0;

		// final progress update, let ppl know it's fully done if it isn't captured already
		if (success)
			update_progress(m_status.total_frames, m_status.total_frames);

		return success;
	}
	catch (const boost::system::system_error& e) {
		u::log_error("Process error: {}", e.what());
		return false;
	}
}

void Render::render() {
	u::log(L"Rendering '{}'\n", m_video_name);

#ifndef _DEBUG
#	ifdef BLUR_GUI
	if (settings.debug) {
		console::init();
	}
	else {
		// todo: close console
		// console::close();
	}
#	endif
#endif

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

	if (do_render(render_commands)) {
		if (blur.verbose) {
			u::log(L"Finished rendering '{}'", m_video_name);
		}
	}
	else {
		u::log(L"Failed to render '{}'", m_video_name);
	}

	// stop preview
	remove_temp_path();
}

void Rendering::stop_rendering() {
	// TODO: re-add

	// // stop vspipe
	// TerminateProcess(vspipe_pi.hProcess, 0);

	// // send wm_close to ffmpeg so that it can gracefully stop
	// if (HWND hwnd = u::get_window(ffmpeg_pi.dwProcessId))
	// 	SendMessage(hwnd, WM_CLOSE, 0, 0);
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
