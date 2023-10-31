#include "rendering.h"

#include "preview.h"

#ifdef BLUR_GUI
#include <gui/console.h>
#endif

void c_rendering::render_videos() {
	if (!queue.empty()) {
		current_render = queue.front();

		try {
			current_render->render();
		}
		catch (const std::exception& e) {
			printf(e.what());
		}

		// finished rendering, delete
		queue.erase(queue.begin());
		current_render = nullptr;

		renders_queued = !queue.empty();
	}
	else {
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}
}

void c_rendering::queue_render(std::shared_ptr<c_render> render) {
	queue.push_back(render);
	renders_queued = true;
}

void c_render::build_output_filename() {
	// build output filename
	int num = 1;
	do {
		std::wstring output_filename = this->video_name + L" - blur";

		if (this->settings.detailed_filenames) {
			std::wstring extra_details;

			// stupid
			if (this->settings.blur) {
				if (this->settings.interpolate) {
					extra_details = fmt::format(L"{}fps ({}, {})", this->settings.blur_output_fps, helpers::towstring(this->settings.interpolated_fps), this->settings.blur_amount);
				}
				else {
					extra_details = fmt::format(L"{}fps ({})", this->settings.blur_output_fps, this->settings.blur_amount);
				}
			}
			else {
				if (this->settings.interpolate) {
					extra_details = fmt::format(L"{}fps", helpers::towstring(this->settings.interpolated_fps));
				}
			}

			if (extra_details != L"") {
				output_filename += L" ~ " + extra_details;
			}
		}

		if (num > 1)
			output_filename += fmt::format(L" ({})", num);

		output_filename += L"." + helpers::towstring(this->settings.video_container);

		this->output_path = this->video_folder / output_filename;

		num++;
	} while (std::filesystem::exists(this->output_path));
}

c_render::c_render(const std::filesystem::path& input_path, std::optional<std::filesystem::path> output_path, std::optional<std::filesystem::path> config_path) {
	this->video_path = input_path;

	this->video_name = this->video_path.stem().wstring();
	this->video_folder = this->video_path.parent_path();

	// parse config file (do it now, not when rendering. nice for batch rendering the same file with different settings)
	if (config_path.has_value())
		this->settings = config.get_config(output_path.value(), false); // specified config path, don't use global
	else
		this->settings = config.get_config(config.get_config_filename(video_folder), true);

	if (output_path.has_value())
		this->output_path = output_path.value();
	else {
		// note: this uses settings, so has to be called after they're loaded
		build_output_filename();
	}
}

bool c_render::create_temp_path() {
	size_t out_hash = std::hash<std::filesystem::path>()(output_path);

	temp_path = std::filesystem::temp_directory_path() / std::string("blur-" + std::to_string(out_hash));

	if (!std::filesystem::create_directory(temp_path))
		return false;

	blur.created_temp_paths.insert(temp_path);

	return true;
}

bool c_render::remove_temp_path() {
	// remove temp path
	if (temp_path.empty())
		return false;

	if (!std::filesystem::exists(temp_path))
		return false;

	std::filesystem::remove_all(temp_path);
	blur.created_temp_paths.erase(temp_path);

	return true;
}

c_render::s_render_command c_render::build_render_command() {
	std::wstring vspipe_path = L"vspipe", ffmpeg_path = L"ffmpeg";
	if (blur.used_installer) {
		vspipe_path = (blur.path / "lib\\vapoursynth\\vspipe.exe").wstring();
		ffmpeg_path = (blur.path / "lib\\ffmpeg\\ffmpeg.exe").wstring();
	}

	std::wstring path_string = video_path.wstring();
	std::replace(path_string.begin(), path_string.end(), '\\', '/');

	std::wstring blur_script_path = (blur.path / "lib\\blur.py").wstring();

	std::stringstream settings_json_ss;
	settings_json_ss << std::quoted(settings.to_json().dump());

	std::wstring pipe_command = fmt::format(L"\"{}\" -c y4m -a video_path=\"{}\" -a settings=\"{}\" -p \"{}\" -", vspipe_path, path_string, helpers::towstring(settings_json_ss.str()), blur_script_path);

	// build ffmpeg command
	std::wstring ffmpeg_command = L'"' + ffmpeg_path + L'"';
	{
		// ffmpeg settings
		ffmpeg_command += L" -loglevel error -hide_banner -stats -y";

		// input
		ffmpeg_command += L" -i -"; // piped output from video script
		ffmpeg_command += fmt::format(L" -i \"{}\"", video_path.wstring()); // original video (for audio)
		ffmpeg_command += L" -map 0:v -map 1:a?"; // copy video from first input, copy audio from second

		// audio filters
		std::wstring audio_filters;
		{
			// timescale (todo: this isn't ideal still, check for a better option)
			if (settings.input_timescale != 1.f) {
				// asetrate: speed up and change pitch
				audio_filters += fmt::format(L"asetrate=48000*{}", (1 / settings.input_timescale));
			}

			if (settings.output_timescale != 1.f) {
				if (audio_filters != L"") audio_filters += L",";
				if (settings.output_timescale_audio_pitch) {
					audio_filters += fmt::format(L"asetrate=48000*{}", settings.output_timescale);
				}
				else {
					// atempo: speed up without changing pitch
					audio_filters += fmt::format(L"atempo={}", settings.output_timescale);
				}
			}
		}

		if (audio_filters != L"")
			ffmpeg_command += L" -af " + audio_filters;

		if (settings.ffmpeg_override != "") {
			ffmpeg_command += L" " + helpers::towstring(settings.ffmpeg_override);
		}
		else {
			// video format
			if (settings.gpu_rendering) {
				if (helpers::to_lower(settings.gpu_type) == "nvidia")
					ffmpeg_command += fmt::format(L" -c:v h264_nvenc -preset p7 -qp {}", settings.quality);
				else if (helpers::to_lower(settings.gpu_type) == "amd")
					ffmpeg_command += fmt::format(L" -c:v h264_amf -qp_i {} -qp_b {} -qp_p {} -quality quality", settings.quality, settings.quality, settings.quality);
				else if (helpers::to_lower(settings.gpu_type) == "intel")
					ffmpeg_command += fmt::format(L" -c:v h264_qsv -global_quality {} -preset veryslow", settings.quality);
			}
			else {
				ffmpeg_command += fmt::format(L" -c:v libx264 -pix_fmt yuv420p -preset superfast -crf {}", settings.quality);
			}

			// audio format
			ffmpeg_command += L" -c:a aac -b:a 320k";

			// extra
			ffmpeg_command += L" -movflags +faststart";
		}

		// output
		ffmpeg_command += fmt::format(L" \"{}\"", output_path.wstring());

		// extra output for preview. generate low-quality preview images.
		if (settings.preview && blur.using_preview) {
			ffmpeg_command += L" -map 0:v"; // copy video from first input
			ffmpeg_command += fmt::format(L" -q:v 2 -update 1 -atomic_writing 1 -y \"{}\"", preview_path.wstring());
		}
	}

	return { pipe_command, ffmpeg_command };
}

bool c_render::do_render(s_render_command render_command) {
	SECURITY_ATTRIBUTES saAttr = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

	// --------------------------------
	/////// run vspipe
	// create stdout pipe to be used as the ffmpeg input
	HANDLE hPipeRead, hPipeWrite;
	if (!CreatePipe(&hPipeRead, &hPipeWrite, &saAttr, 0)) {
		std::cerr << "Error creating pipe." << std::endl;
		return false;
	}

	// create stderr pipe to track progress
	HANDLE hPipeReadErr, hPipeWriteErr;
	if (!CreatePipe(&hPipeReadErr, &hPipeWriteErr, &saAttr, 0)) {
		std::cerr << "Error creating pipe." << std::endl;
		return false;
	}

	STARTUPINFO vspipe_si;
	SecureZeroMemory(&vspipe_si, sizeof(STARTUPINFO));
	SecureZeroMemory(&rendering.vspipe_pi, sizeof(PROCESS_INFORMATION));

	vspipe_si.cb = sizeof(vspipe_si);

	vspipe_si.dwFlags = STARTF_USESTDHANDLES;
	vspipe_si.hStdOutput = hPipeWrite;
	vspipe_si.hStdError = hPipeWriteErr;

	std::wstring vspipe_command = render_command.pipe_command;
	if (settings.debug) wprintf(L"%s\n", vspipe_command.c_str());
	BOOL bSuccess = CreateProcess(nullptr, vspipe_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &vspipe_si, &rendering.vspipe_pi);

	CloseHandle(rendering.vspipe_pi.hThread);
	CloseHandle(hPipeWrite);
	CloseHandle(hPipeWriteErr);

	if (!bSuccess) {
		CloseHandle(hPipeRead);
		CloseHandle(hPipeReadErr);
		CloseHandle(rendering.vspipe_pi.hProcess);
		std::cerr << "Error creating process." << std::endl;
		return false;
	}

	// --------------------------------
	/////// run ffmpeg
	STARTUPINFO ffmpeg_si;
	SecureZeroMemory(&ffmpeg_si, sizeof(STARTUPINFO));
	SecureZeroMemory(&rendering.ffmpeg_pi, sizeof(PROCESS_INFORMATION));
	ffmpeg_si.cb = sizeof(ffmpeg_si);

	ffmpeg_si.hStdInput = hPipeRead; // use the vspipe stdout as input (piping)
	ffmpeg_si.dwFlags = STARTF_USESTDHANDLES;

	std::wstring ffmpeg_command = render_command.ffmpeg_command;
	if (settings.debug) wprintf(L"%s\n", ffmpeg_command.c_str());
	bool success = CreateProcess(nullptr, ffmpeg_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &ffmpeg_si, &rendering.ffmpeg_pi);

	CloseHandle(rendering.ffmpeg_pi.hThread);

	if (!success) {
		CloseHandle(hPipeRead);
		CloseHandle(hPipeReadErr);
		CloseHandle(rendering.vspipe_pi.hProcess);
		CloseHandle(rendering.ffmpeg_pi.hProcess);
		return false;
	}

	std::thread read_thread([&]() {
		status.start_time = std::chrono::high_resolution_clock::now();

		helpers::read_line_from_handle(hPipeReadErr, [this](std::string line) {
			std::regex frame_regex(R"(Frame: (\d+)\/(\d+)(?: \((\d+\.\d+) fps\))?)");

			std::smatch match;
			if (std::regex_match(line, match, frame_regex)) {
				status.current_frame = std::stoi(match[1]);
				status.total_frames = std::stoi(match[2]);

				if (!status.init)
					status.init = true;

				std::cout << status.progress_string() << "\n";
			}
		});
	});

	// wait for rendering to complete
	WaitForSingleObject(rendering.ffmpeg_pi.hProcess, INFINITE);

	// clean up
	CloseHandle(hPipeRead);
	CloseHandle(hPipeReadErr);
	CloseHandle(rendering.vspipe_pi.hProcess);
	CloseHandle(rendering.ffmpeg_pi.hProcess);

	// stop reading thread
	if (read_thread.joinable())
		read_thread.join();

	return true;
}

void c_render::render() {
	wprintf(L"Rendering '%s'\n", video_name.c_str());

#ifndef _DEBUG
#ifdef BLUR_GUI
	if (settings.debug) {
		console::init();
	}
	else {
		// todo: close console
		// console::close();
	}
#endif
#endif

	if (blur.verbose) {
		printf("Render settings:\n");
		printf("Source video at %.2f timescale\n", settings.input_timescale);
		if (settings.interpolate) printf("Interpolated to %sfps with %.2f timescale\n", settings.interpolated_fps.c_str(), settings.output_timescale);
		if (settings.blur) printf("Motion blurred to %dfps (%d%%)\n", settings.blur_output_fps, static_cast<int>(settings.blur_amount * 100));
		printf("Rendered at %.2f speed with crf %d\n", settings.output_timescale, settings.quality);
	}

	// start preview
	if (settings.preview && blur.using_preview) {
		if (create_temp_path()) {
			preview_path = temp_path / "blur_preview.jpg";
			preview.start(preview_path);
		}
	}

	// render
	auto render_command = build_render_command();
	
	if (do_render(render_command)) {
		if (blur.verbose) {
			wprintf(L"Finished rendering '%s'\n", video_name.c_str());
		}
	}
	else {
		wprintf(L"Failed to render '%s'\n", video_name.c_str());
	}

	// stop preview
	preview.disable();
	remove_temp_path();
}

void c_rendering::stop_rendering() {
	TerminateProcess(vspipe_pi.hProcess, 0);

	// send wm_close to ffmpeg so that it can gracefully stop
	if (HWND hwnd = helpers::get_window(ffmpeg_pi.dwProcessId))
		SendMessage(hwnd, WM_CLOSE, 0, 0);
}

std::string s_render_status::progress_string() {
	if (!init)
		return "";

	float progress = current_frame / (float)total_frames;

	auto current_time = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> frame_duration = current_time - start_time;
	double elapsed_time = frame_duration.count();

	double calced_fps = current_frame / elapsed_time;

	return fmt::format("{:.1f}% complete ({}/{}, {:.2f} fps)", progress * 100, current_frame, total_frames, calced_fps);
}
