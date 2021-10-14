#include "includes.h"

void c_rendering::render_videos() {
	auto& queue = rendering.queue;

	while (!queue.empty()) {
		auto render = queue.front();

		try {
			render.render();
		}
		catch (const std::exception& e) {
			console.print(e.what());
		}

		// finished rendering, delete
		queue.erase(queue.begin());
	}

	rendering.renders_queued = false;
}

void c_rendering::render_videos_thread() {
	while (true) {
		if (!queue.empty()) {
			auto render = queue.front();

			try {
				render.render();
			}
			catch (const std::exception& e) {
				console.print(e.what());
				if (blur.using_ui) {
					console.print_blank_line();
				}
			}

			// finished rendering, delete
			queue.erase(queue.begin());

			renders_queued = !queue.empty();

			if (!renders_queued) {
				if (blur.using_ui) {
					console.print_line();
					console.print("finished render queue, waiting for input.");
				}
			}
		}
		else {
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
	}
}

void c_rendering::start_thread() {
	if (thread_ptr)
		return;

	thread_ptr = std::unique_ptr<std::thread>(new std::thread(&c_rendering::render_videos_thread, this));
	thread_ptr->detach();
}

void c_rendering::stop_thread() {
	if (!thread_ptr)
		return;

	thread_ptr->join();
}

void c_rendering::queue_render(c_render render) {
	queue.push_back(render);
	renders_queued = true;
}

void c_render::build_output_filename() {
	// build output filename
	int num = 1;
	do {
		this->output_path = this->video_folder + fmt::format("{} - blur", this->video_name);

		if (this->settings.detailed_filenames) {
			std::string extra_details;

			// stupid
			if (this->settings.blur) {
				if (this->settings.interpolate) {
					extra_details = fmt::format("{}fps ({}, {})", this->settings.blur_output_fps, this->settings.interpolated_fps, this->settings.blur_amount);
				}
				else {
					extra_details = fmt::format("{}fps ({})", this->settings.blur_output_fps, this->settings.blur_amount);
				}
			}
			else {
				if (this->settings.interpolate) {
					extra_details = fmt::format("{}fps", this->settings.interpolated_fps);
				}
			}

			if (extra_details != "") {
				this->output_path += " ~ " + extra_details;
			}
		}

		if (num > 1)
			this->output_path += fmt::format(" ({})", num);

		this->output_path += ".mp4";

		num++;
	} while (std::filesystem::exists(this->output_path));
}

c_render::c_render(const std::string& input_path, std::optional<std::string> output_filename, std::optional<std::string> config_path) {
	this->video_path = input_path;

	this->video_name = std::filesystem::path(this->video_path).stem().string();
	this->video_folder = std::filesystem::path(this->video_path).parent_path().string() + "\\";

	this->input_filename = std::filesystem::path(this->video_path).filename().string();

	if (blur.using_ui) {
		// print message
		if (!rendering.queue.empty()) {
			console.print_blank_line();
			console.print_line();
		}

		console.print(fmt::format("opening {} for processing", this->input_filename));
	}

	if (config_path.has_value()) {
		bool first_time_config = false;
		this->settings = config.parse(config_path.value(), first_time_config);

		if (first_time_config) {
			if (blur.verbose)
				console.print(fmt::format("configuration file not found, default config generated at {}", config_path.value()));
		}
	}
	else {
		// parse config file (do it now, not when rendering. nice for batch rendering the same file with different settings)
		bool first_time_config = false;
		std::string config_filepath;
		this->settings = config.parse_folder(this->video_folder, config_filepath, first_time_config);

		if (blur.using_ui) {
			// check if the config exists
			if (first_time_config) {
				console.print_blank_line();
				console.print(fmt::format("configuration file not found, default config generated at {}", config_filepath));
				console.print_blank_line();
				console.print("continue render? (y/n)");
				console.print_blank_line();

				char choice = console.get_char();
				if (tolower(choice) != 'y') {
					throw std::exception("stopping render");
				}

				// get settings again in case the user modified them
				this->settings = config.parse_folder(this->video_folder, config_filepath, first_time_config);
			}
		}
	}

	if (output_filename.has_value())
		this->output_path = output_filename.value();
	else
		this->build_output_filename();

	if (blur.using_ui) {
		console.print(fmt::format("writing to {}", this->output_path));

		console.print_line();
	}
}

std::string c_render::build_ffmpeg_command() {
	std::string vspipe_path = "vspipe", ffmpeg_path = "ffmpeg";
	if (blur.used_installer) {
		vspipe_path = fmt::format("\"{}/lib/vapoursynth/vspipe.exe\"", blur.path);
		ffmpeg_path = fmt::format("\"{}/lib/ffmpeg/ffmpeg.exe\"", blur.path);
	}

	std::string pipe_command = fmt::format("{} -y \"{}\" -", vspipe_path, script_handler.script_filename);

	// build ffmpeg command
	std::string ffmpeg_command = ffmpeg_path;
	{
		// ffmpeg settings
		ffmpeg_command += " -loglevel error -hide_banner -stats";

		// input
		ffmpeg_command += " -i -"; // piped output from video script
		ffmpeg_command += fmt::format(" -i \"{}\"", video_path); // original video (for audio)
		ffmpeg_command += " -map 0:v -map 1:a?"; // copy video from first input, copy audio from second

		// audio filters
		std::string audio_filters;
		{
			// timescale (todo: this isn't ideal still, check for a better option)
			if (settings.input_timescale != 1.f) {
				// asetrate: speed up and change pitch
				audio_filters += fmt::format("asetrate=48000*{}", (1 / settings.input_timescale));
			}

			if (settings.output_timescale != 1.f) {
				if (audio_filters != "") audio_filters += ",";
				if (settings.output_timescale_audio_pitch) {
					audio_filters += fmt::format("asetrate=48000*{}", settings.output_timescale);
				}
				else {
					// atempo: speed up without changing pitch
					audio_filters += fmt::format("atempo={}", settings.output_timescale);
				}
			}
		}

		if (audio_filters != "")
			ffmpeg_command += " -af " + audio_filters;

		if (settings.ffmpeg_override != "") {
			ffmpeg_command += " " + settings.ffmpeg_override;
		} else {
			// video format
			if (settings.gpu) {
				if (helpers::to_lower(settings.gpu_type) == "nvidia")
					ffmpeg_command += fmt::format(" -c:v h264_nvenc -preset p7 -qp {}", settings.quality);
				else if (helpers::to_lower(settings.gpu_type) == "amd")
					ffmpeg_command += fmt::format(" -c:v h264_amf -qp_i {} -qp_b {} -qp_p {} -quality quality", settings.quality, settings.quality, settings.quality);
				else if (helpers::to_lower(settings.gpu_type) == "intel")
					ffmpeg_command += fmt::format(" -c:v h264_qsv -global_quality {} -preset veryslow", settings.quality);
			}
			else {
				ffmpeg_command += fmt::format(" -c:v libx264 -pix_fmt yuv420p -preset superfast -crf {}", settings.quality);
			}

			// audio format
			ffmpeg_command += " -c:a aac -b:a 320k";

			// extra
			ffmpeg_command += " -movflags +faststart";
		}

		// output
		ffmpeg_command += fmt::format(" \"{}\"", output_path);

		// extra output for preview. generate low-quality preview images.
		if (settings.preview && !blur.no_preview) {
			ffmpeg_command += " -map 0:v"; // copy video from first input
			ffmpeg_command += fmt::format(" -q:v 3 -update 1 -atomic_writing 1 -y \"{}\"", preview_filename);
		}
	}

	return pipe_command + " | " + ffmpeg_command;
}

void c_render::render() {
	if (blur.verbose) {
		console.print(fmt::format("rendering {}...", video_name));
		console.print_blank_line();
	}

	// create temp path
	temp_path = blur.create_temp_path(video_folder);

	if (blur.verbose) {
		console.print(fmt::format("render settings:"));
		console.print(fmt::format("- source video at {:.2f} timescale -", settings.input_timescale));
		if (settings.interpolate) console.print(fmt::format("- interpolated to {}fps with {:.2f} timescale -", settings.interpolated_fps, settings.output_timescale));
		if (settings.blur) console.print(fmt::format("- motion blurred to {}fps ({}%) -", settings.blur_output_fps, static_cast<int>(settings.blur_amount * 100)));
		console.print(fmt::format("- rendered at {:.2f} speed with crf {} -", settings.output_timescale, settings.quality));

		console.print_line();
	}

	// create script
	script_handler.create(temp_path, video_path, settings);

	// start preview
	if (settings.preview && !blur.no_preview) {
		preview_filename = temp_path + "preview.jpg";
		preview.start(preview_filename);
	}

	// render
	auto ffmpeg_settings = build_ffmpeg_command();
	helpers::exec(ffmpeg_settings);

	// finished render
	if (blur.verbose) {
		console.print_blank_line();
		console.print(fmt::format("finished rendering {}", video_name));
	}

	// remove temp path and files inside
	blur.remove_temp_path(temp_path);

	preview.disable();
}