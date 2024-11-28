#include "rendering_frame.h"

RenderCommands FrameRender::build_render_commands(
	const std::filesystem::path& input_path, const std::filesystem::path& output_path, const BlurSettings& settings
) {
	RenderCommands commands;

	if (blur.used_installer) {
		commands.vspipe_path = (blur.path / "lib\\vapoursynth\\vspipe.exe").wstring();
		commands.ffmpeg_path = (blur.path / "lib\\ffmpeg\\ffmpeg.exe").wstring();
	}
	else {
		commands.vspipe_path = blur.vspipe_path.wstring();
		commands.ffmpeg_path = blur.ffmpeg_path.wstring();
	}

	std::wstring path_string = input_path.wstring();
	std::ranges::replace(path_string, '\\', '/');

	std::wstring blur_script_path = (blur.path / "lib/blur.py").wstring();

	// Build vspipe command
	commands.vspipe = { L"-p",
		                L"-c",
		                L"y4m",
		                L"-a",
		                L"video_path=" + path_string,
		                L"-a",
		                // L"-s",
		                // L"1",
		                // L"-e",
		                // L"2",
		                L"settings=" + u::towstring(settings.to_json().dump()),
		                blur_script_path,
		                L"-" };

	// Build ffmpeg command
	// clang-format off
	commands.ffmpeg = {
		L"-loglevel",
		L"error",
		L"-hide_banner",
		L"-stats",
		L"-ss",
		L"00:00:01",
		L"-y",
		L"-i",
		L"-", // piped output from video script
		L"-vframes",
		L"1",
		L"-y",
		output_path.wstring(),
	};
	// clang-format on

	return commands;
}

bool FrameRender::do_render(RenderCommands render_commands, const BlurSettings& settings) {
	namespace bp = boost::process;

	try {
		boost::asio::io_context io_context;
		bp::pipe vspipe_stdout;

		if (settings.debug) {
			u::log(L"VSPipe command: {} {}", render_commands.vspipe_path, u::join(render_commands.vspipe, L" "));
			u::log(L"FFmpeg command: {} {}", render_commands.ffmpeg_path, u::join(render_commands.ffmpeg, L" "));
		}

		running = false;

		// Declare as local variables first, then move or assign
		vspipe_process = bp::child(
			render_commands.vspipe_path,
			bp::args(render_commands.vspipe),
			bp::std_out > vspipe_stdout,
			io_context
#ifdef _WIN32
			,
			bp::windows::create_no_window
#endif
		);

		ffmpeg_process = bp::child(
			render_commands.ffmpeg_path,
			bp::args(render_commands.ffmpeg),
			bp::std_in < vspipe_stdout,
			bp::std_out.null(),
			io_context
#ifdef _WIN32
			,
			bp::windows::create_no_window
#endif
		);

		vspipe_process.detach();
		ffmpeg_process.detach();

		running = true;

		while (vspipe_process.running() || ffmpeg_process.running()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		if (settings.debug)
			u::log(
				"vspipe exit code: {}, ffmpeg exit code: {}", vspipe_process.exit_code(), ffmpeg_process.exit_code()
			);

		bool success = vspipe_process.exit_code() == 0 && ffmpeg_process.exit_code() == 0;

		return success;
	}
	catch (const boost::system::system_error& e) {
		u::log_error("Process error: {}", e.what());
		return false;
	}
}

FrameRender::RenderResponse FrameRender::render(const std::filesystem::path& input_path, const BlurSettings& settings) {
	std::filesystem::path output_path = input_path.parent_path() / "temp.jpg";

	// render
	RenderCommands render_commands = build_render_commands(input_path, output_path, settings);

	bool render_success = do_render(render_commands, settings);

	return {
		.success = render_success,
		.output_path = output_path,
	};
}

void FrameRender::kill() {
	if (!running)
		return;

	if (vspipe_process.valid()) {
		try {
			vspipe_process.terminate();
		}
		catch (const std::exception& e) {
			u::log_error("error terminating vspipe: {}", e.what());
		}
	}

	if (ffmpeg_process.valid()) {
		try {
			ffmpeg_process.terminate();
		}
		catch (const std::exception& e) {
			u::log_error("error terminating ffmpeg: {}", e.what());
		}
	}
}
