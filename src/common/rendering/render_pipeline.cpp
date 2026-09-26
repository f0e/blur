#include "render_pipeline.h"
#include "common/vspipe.h"

namespace bp = boost::process;

namespace {
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

	// vspipe's progress lines end in \r, errors end in \n
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
					// python writes \r\n on windows, so leave the line for the \n to end
					if (vspipe_stderr.peek() == '\n')
						continue;

					// not a frame update, e.g. a status line from a tensorrt engine build
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

	// find complete jpeg frames (FFD8..FFD9) in ffmpeg's stdout for the preview. reads in big chunks, byte by byte
	// can't keep up and stalls the render
	void extract_jpeg_stream(bp::ipstream& ffmpeg_stdout, const std::shared_ptr<rendering::RenderState>& state) {
		if (!state->preview_capture_enabled())
			return;

		static constexpr size_t CHUNK_SIZE = size_t{ 64 } * 1024;
		static constexpr size_t INITIAL_JPEG_CAPACITY = size_t{ 1024 } * 512;

		// fast renders make preview frames far quicker than they can be shown, and each one costs a decode and upload.
		// throttled here since ffmpeg can only cap it in video time, which would starve slow renders
		static constexpr auto MIN_PREVIEW_INTERVAL = std::chrono::milliseconds(50);

		std::array<char, CHUNK_SIZE> chunk{};

		std::vector<uint8_t> buf;
		buf.reserve(INITIAL_JPEG_CAPACITY);

		std::vector<uint8_t> pending; // most recent frame the throttle skipped

		bool in_jpeg = false;
		bool trailing_ff = false; // the previous chunk ended in 0xFF (a start marker can straddle chunks)

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
					// consume up to and including the next FFD9 end marker (FFD9 only appears as EOI since 0xFF is
					// stuffed)
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

							buf = {};
							buf.reserve(INITIAL_JPEG_CAPACITY);
						}
						else {
							// too soon after the last one, but keep it in case it's the final frame
							std::swap(pending, buf);
							buf.clear();
						}

						in_jpeg = false;
					}
				}
			}
		}

		// make sure the last frame is the one left on screen
		if (!pending.empty())
			state->set_preview_jpeg(std::move(pending));
	}

	// turn a non-zero exit into an error, preferring a parsed blur exception. each process's stderr is kept separate
	rendering::RenderError assemble_render_error(const std::string& vspipe_errors, const std::string& ffmpeg_errors) {
		rendering::RenderError err;

		auto parsed = rendering::detail::parse_error_output(vspipe_errors);
		if (parsed) {
			err = *parsed;
		}
		else {
			err.user_message = "An unexpected error occurred";
			err.is_blur_exception = false;
		}

		// the parsed blur exception blobs are removed, but keep what the script logged before it
		err.vspipe_errors =
			err.is_blur_exception ? rendering::detail::without_error_objects(vspipe_errors) : vspipe_errors;
		err.ffmpeg_errors = ffmpeg_errors;

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
		auto env = vspipe::setup_environment();

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

		auto spawn_error = [](const std::string& details) {
			return tl::unexpected(
				RenderError{
					.user_message = "Failed to start rendering",
					.technical_details = details,
					.is_blur_exception = false,
				}
			);
		};

		if (!vspipe_process)
			return spawn_error(vspipe_process.error());

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

		if (!ffmpeg_process) {
			u::safe_terminate(vspipe_group);
			return spawn_error(ffmpeg_process.error());
		}

		// only once both have spawned - if either throws, joinable threads going out of scope would terminate the app
		std::thread vspipe_stderr_thread(
			pump_vspipe_stderr, std::ref(vspipe_stderr), state, std::ref(vspipe_errors), progress_callback
		);
		std::thread ffmpeg_stderr_thread(pump_ffmpeg_stderr, std::ref(ffmpeg_stderr), std::ref(ffmpeg_errors));
		std::thread ffmpeg_stdout_thread(extract_jpeg_stream, std::ref(ffmpeg_stdout), state);

		bool killed = false;
		while (ffmpeg_process->running()) {
			if (state->wants_stop() || blur.exiting) {
				u::safe_terminate(vspipe_group);
				u::safe_terminate(*ffmpeg_process);
				killed = true;
				break;
			}

			if (state->wants_pause() != state->is_paused()) {
				auto fn = state->wants_pause() ? suspend_render : resume_render;
				fn(vspipe_process->id(), state);
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
		vspipe_process->wait(wait_ec);

		// if vspipe fails mid-render ffmpeg can still exit 0 with an empty video, so check vspipe too
		bool vspipe_failed = vspipe_process->exit_code() != 0;

		if (vspipe_failed || ffmpeg_process->exit_code() != 0) {
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
