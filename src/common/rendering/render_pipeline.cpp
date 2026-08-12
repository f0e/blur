#include "render_pipeline.h"

namespace bp = boost::process;

namespace {
	// suspend the render process at the OS level and record it in the state
	void suspend_render(int pid, const std::shared_ptr<rendering::RenderState>& state) {
		if (state->is_paused())
			return;

		if (pid > 0) {
#ifdef WIN32
			u::windows_toggle_suspend_process(pid, true);
#else
			kill(pid, SIGSTOP);
#endif
		}

		state->mark_paused(true);

		u::log("Render paused");
	}

	void resume_render(int pid, const std::shared_ptr<rendering::RenderState>& state) {
		if (!state->is_paused())
			return;

		if (pid > 0) {
#ifdef WIN32
			u::windows_toggle_suspend_process(pid, false);
#else
			kill(pid, SIGCONT);
#endif
		}

		state->mark_paused(false);

		u::log("Render resumed");
	}

	// vspipe reports progress on stderr as "\r"-terminated "Frame: n/m" lines,
	// interleaved with "\n"-terminated actual error output
	void pump_vspipe_stderr(
		bp::ipstream& vspipe_stderr,
		const std::shared_ptr<rendering::RenderState>& state,
		std::ostringstream& vspipe_errors,
		const std::function<void()>& progress_callback
	) {
		std::string line;
		char ch = 0;
		while (vspipe_stderr.get(ch)) {
			if (ch == '\r') {
				static std::regex frame_regex(R"(Frame: (\d+)\/(\d+)(?: \((\d+\.\d+) fps\))?)");

				std::smatch match;
				if (std::regex_match(line, match, frame_regex)) {
					state->report_frame_progress(std::stoi(match[1]), std::stoi(match[2]));

					if (progress_callback)
						progress_callback();
				}
				else {
					// not a frame update - e.g. a \r-terminated status line from a
					// TensorRT engine build.
					state->report_log_line(line);
				}

				line.clear();
				continue;
			}

			if (ch == '\n') {
				vspipe_errors << line << '\n';

				DEBUG_LOG("[vspipe error] {}", line);

				state->report_log_line(line);

				line.clear();
				continue;
			}

			line += ch;
		}

		if (!line.empty()) {
			vspipe_errors << line << '\n';
			DEBUG_LOG("[vspipe error] {}", line);
			state->report_log_line(line);
		}
	}

	void pump_ffmpeg_stderr(bp::ipstream& ffmpeg_stderr, std::ostringstream& ffmpeg_errors) {
		std::string line;
		while (std::getline(ffmpeg_stderr, line)) {
			ffmpeg_errors << line << '\n';

			DEBUG_LOG("[ffmpeg error] {}", line);
		}
	}

	// scan ffmpeg's stdout for complete jpeg frames (FFD8..FFD9) and hand each
	// one to the state as the latest preview
	void extract_jpeg_stream(bp::ipstream& ffmpeg_stdout, const std::shared_ptr<rendering::RenderState>& state) {
		if (!state->preview_capture_enabled())
			return;

		std::vector<uint8_t> buf;
		buf.reserve(1024 * 512);

		uint8_t byte;
		bool in_jpeg = false;

		while (ffmpeg_stdout.read(reinterpret_cast<char*>(&byte), 1)) {
			if (!in_jpeg) {
				if (byte == 0xFF) {
					buf.push_back(byte);
				}
				else if (!buf.empty() && byte == 0xD8) {
					buf.push_back(byte);
					in_jpeg = true;
				}
				else {
					buf.clear();
				}
			}
			else {
				buf.push_back(byte);

				if (buf.size() >= 2 && buf[buf.size() - 2] == 0xFF && buf[buf.size() - 1] == 0xD9) {
					state->set_preview_jpeg(buf);

					buf.clear();
					in_jpeg = false;
				}
			}
		}
	}

	// turn a non-zero exit into a user-facing error, preferring a parsed blur
	// exception and otherwise attaching the raw stderr streams for debugging
	rendering::RenderError assemble_render_error(const std::string& vspipe_errors, const std::string& ffmpeg_errors) {
		std::string process_errors;
		if (!vspipe_errors.empty()) {
			process_errors += "--- [vspipe] ---\n" + vspipe_errors + "\n";
		}

		if (!ffmpeg_errors.empty()) {
			process_errors += "--- [ffmpeg] ---\n" + ffmpeg_errors;
		}

		rendering::RenderError err;

		auto parsed = u::parse_error_output(vspipe_errors);
		if (parsed) {
			err = *parsed;
		}
		else {
			err.user_message = "An unexpected error occurred";
			err.is_blur_exception = false;
		}

		// if exception isnt coming from blur, include process stderr streams for debugging
		if (!err.is_blur_exception) {
			if (!err.technical_details.empty()) {
				err.technical_details += "\n\n";
			}

			err.technical_details += process_errors;
		}

		return err;
	}
}

tl::expected<rendering::detail::PipelineResult, rendering::RenderError> rendering::detail::execute_pipeline(
	const RenderCommands& commands,
	const std::shared_ptr<RenderState>& state,
	bool debug,
	bool audio,
	const std::function<void()>& progress_callback
) {
	try {
		auto env = u::setup_vspipe_environment();

		bp::pipe vspipe_stdout;
		bp::ipstream ffmpeg_stdout;

		bp::ipstream vspipe_stderr;
		bp::ipstream ffmpeg_stderr;

		std::ostringstream vspipe_errors;
		std::ostringstream ffmpeg_errors;

#ifndef _DEBUG
		if (debug)
#endif
		{
			DEBUG_LOG("VSPipe video: {} {}", blur.vspipe_path, u::join(commands.vspipe_video, " "));
			DEBUG_LOG("FFmpeg: {} {}", blur.ffmpeg_path, u::join(commands.ffmpeg, " "));
		}

		std::thread vspipe_stderr_thread(
			pump_vspipe_stderr, std::ref(vspipe_stderr), state, std::ref(vspipe_errors), progress_callback
		);
		std::thread ffmpeg_stderr_thread(pump_ffmpeg_stderr, std::ref(ffmpeg_stderr), std::ref(ffmpeg_errors));
		std::thread ffmpeg_stdout_thread(extract_jpeg_stream, std::ref(ffmpeg_stdout), state);

		// tensorrt spawns trtexec as a grandchild, so group to terminate it as well
		bp::group vspipe_group;

		auto vspipe_process = u::run_command(
			blur.vspipe_path,
			commands.vspipe_video,
			env,
			bp::std_out > vspipe_stdout,
			bp::std_err > vspipe_stderr,
			bp::std_in < bp::null, // stdin is an invalid handle otherwise, which breaks
		                           // subprocess.run(stdout=sys.stderr) in rife-trt (FUN!)
			vspipe_group
		);

		auto ffmpeg_process =
			state->preview_capture_enabled()
				? u::run_command(
					  blur.ffmpeg_path,
					  commands.ffmpeg,
					  env,
					  bp::std_out > ffmpeg_stdout,
					  bp::std_err > ffmpeg_stderr,
					  bp::std_in < vspipe_stdout
				  )
				: u::run_command(
					  blur.ffmpeg_path, commands.ffmpeg, env, bp::std_err > ffmpeg_stderr, bp::std_in < vspipe_stdout
				  );

		bool killed = false;
		while (ffmpeg_process.running()) {
			if (state->wants_stop()) {
				u::safe_terminate(vspipe_group);
				u::safe_terminate(ffmpeg_process);
				killed = true;
				break;
			}

			if (state->wants_pause() != state->is_paused()) {
				auto fn = state->wants_pause() ? suspend_render : resume_render;
				fn(vspipe_process.id(), state);
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		// stop stuff if they're stuck (no-op if already terminated above)
		u::safe_terminate(vspipe_group);

		// wait for threads to finish
		if (ffmpeg_stdout_thread.joinable())
			ffmpeg_stdout_thread.join();

		if (ffmpeg_stderr_thread.joinable())
			ffmpeg_stderr_thread.join();

		if (vspipe_stderr_thread.joinable())
			vspipe_stderr_thread.join();

		if (killed)
			return PipelineResult{ .stopped = true };

		if (ffmpeg_process.exit_code() != 0) {
			return tl::unexpected(assemble_render_error(vspipe_errors.str(), ffmpeg_errors.str()));
		}

		return PipelineResult{ .stopped = false };
	}
	catch (const std::exception& e) {
		return tl::unexpected(
			RenderError{
				.user_message = "An unexpected error occurred",
				.technical_details = std::string("C++ exception: ") + e.what(),
				.is_blur_exception = false,
			}
		);
	}
}
