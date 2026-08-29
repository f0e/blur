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
	// one to the state as the latest preview.
	// note: reads in big chunks - pulling a byte at a time out of the stream can't
	// keep up with ffmpeg's output, which backpressures the pipe and stalls the render
	void extract_jpeg_stream(bp::ipstream& ffmpeg_stdout, const std::shared_ptr<rendering::RenderState>& state) {
		if (!state->preview_capture_enabled())
			return;

		static constexpr size_t CHUNK_SIZE = 64 * 1024;
		static constexpr size_t INITIAL_JPEG_CAPACITY = 1024 * 512;

		// a render going much faster than realtime produces preview frames far quicker than
		// anything can display them, and every one the gui picks up costs a jpeg decode and a
		// texture upload on the main thread. throttled here rather than in ffmpeg because
		// ffmpeg can only cap this in video time, which would starve slow renders of previews
		static constexpr auto MIN_PREVIEW_INTERVAL = std::chrono::milliseconds(50);

		std::array<char, CHUNK_SIZE> chunk{};

		std::vector<uint8_t> buf;
		buf.reserve(INITIAL_JPEG_CAPACITY);

		std::vector<uint8_t> pending; // most recent frame the throttle skipped

		bool in_jpeg = false;
		bool trailing_ff = false; // last byte of the previous chunk was 0xFF (start marker may straddle chunks)

		auto last_handoff = std::chrono::steady_clock::now() - MIN_PREVIEW_INTERVAL; // let the first frame through

		while (ffmpeg_stdout.read(chunk.data(), chunk.size()) || ffmpeg_stdout.gcount() > 0) {
			const auto* data = reinterpret_cast<const uint8_t*>(chunk.data());
			size_t size = ffmpeg_stdout.gcount();
			size_t pos = 0;

			while (pos < size) {
				if (!in_jpeg) {
					// look for the FFD8 start marker, skipping anything before it
					if (trailing_ff) {
						trailing_ff = false;

						if (data[pos] == 0xD8) {
							buf.assign({ 0xFF, 0xD8 });
							in_jpeg = true;
							pos++;
							continue;
						}
					}

					const auto* ff = static_cast<const uint8_t*>(std::memchr(data + pos, 0xFF, size - pos));
					if (!ff)
						break;

					size_t ff_pos = ff - data;
					if (ff_pos + 1 == size) {
						trailing_ff = true;
						break;
					}

					if (data[ff_pos + 1] == 0xD8) {
						buf.assign({ 0xFF, 0xD8 });
						in_jpeg = true;
						pos = ff_pos + 2;
					}
					else {
						pos = ff_pos + 1;
					}
				}
				else {
					// consume up to and including the next FFD9 end marker (0xFF bytes inside
					// entropy-coded data are always stuffed, so FFD9 only appears as EOI)
					const auto* d9 = static_cast<const uint8_t*>(std::memchr(data + pos, 0xD9, size - pos));
					if (!d9) {
						buf.insert(buf.end(), data + pos, data + size);
						break;
					}

					size_t end = (d9 - data) + 1;
					buf.insert(buf.end(), data + pos, data + end);
					pos = end;

					if (buf.size() >= 2 && buf[buf.size() - 2] == 0xFF) {
						auto now = std::chrono::steady_clock::now();

						if (now - last_handoff >= MIN_PREVIEW_INTERVAL) {
							state->set_preview_jpeg(std::move(buf));
							last_handoff = now;
							pending.clear();

							buf = {}; // moved from, reset it properly
							buf.reserve(INITIAL_JPEG_CAPACITY);
						}
						else {
							// came too soon after the last one, but hold onto it in case it
							// turns out to be the final frame (swapped so the buffers get reused)
							std::swap(pending, buf);
							buf.clear();
						}

						in_jpeg = false;
					}
				}
			}
		}

		// the stream's ended, so make sure the last frame rendered is the one left on screen
		if (!pending.empty())
			state->set_preview_jpeg(std::move(pending));
	}

	// turn a non-zero exit into a user-facing error, preferring a parsed blur exception. keep each process's
	// stderr separate so the UI can present it clearly; RenderError::to_string combines them for support copies.
	rendering::RenderError assemble_render_error(const std::string& vspipe_errors, const std::string& ffmpeg_errors) {
		rendering::RenderError err;

		auto parsed = u::parse_error_output(vspipe_errors);
		if (parsed) {
			err = *parsed;
		}
		else {
			err.user_message = "An unexpected error occurred";
			err.is_blur_exception = false;
		}

		// a parsed blur exception already says everything worth saying, and the stderr it came out of is just the
		// json blob it was parsed from - showing that back to the user is noise
		if (!err.is_blur_exception) {
			err.vspipe_errors = vspipe_errors;
			err.ffmpeg_errors = ffmpeg_errors;
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

		std::error_code wait_ec;
		vspipe_process.wait(wait_ec);

		// anything the script only hits once it's rendering - a plugin that won't run on this machine, a source
		// that fails partway - throws when a frame is asked for, by which point vspipe has already written the
		// y4m header. ffmpeg then muxes a file with zero video frames in it and exits 0 quite happily (the mp4
		// muxer drops the empty track entirely), so vspipe's exit code is the only sign anything went wrong
		bool vspipe_failed = !commands.ffmpeg_stops_early && vspipe_process.exit_code() != 0;

		if (vspipe_failed || ffmpeg_process.exit_code() != 0) {
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
