#include "includes.h"

bool detect_command(std::string command) {
	std::string find_ffmpeg_command = "where /q " + command;

	return system(find_ffmpeg_command.c_str()) == 0;
}

std::string c_rendering::get_ffmpeg_command(const s_blur_settings& settings, const std::string& output_name, const std::string& preview_name) {
	// check if ffmpeg is installed
	if (!detect_command("ffmpeg"))
		throw std::exception("ffmpeg could not be found");

	std::vector<std::string> ffmpeg_settings;
	if (settings.gpu) {
		ffmpeg_settings = {
			"ffmpeg",
			"-loglevel error -hide_banner -stats", // only show progress
			fmt::format("-i \"{}\"", avisynth.get_filename()), // input file
			"-c:v h264_nvenc -pix_fmt yuv420p", // render format settings
			fmt::format("-cq {}", settings.crf), // main render settings
			fmt::format("\"{}\"", output_name) // output file
		};	
	}
	else {
		ffmpeg_settings = {
			"ffmpeg",
			"-loglevel error -hide_banner -stats", // only show progress
			fmt::format("-i \"{}\"", avisynth.get_filename()), // input file
			"-c:v libx264 -pix_fmt yuv420p", // render format settings
			fmt::format("-preset superfast -crf {}", settings.crf), // main render settings
			fmt::format("\"{}\"", output_name) // output file
		};
	}

	if (settings.preview) {
		ffmpeg_settings.push_back(
			fmt::format("-q:v 5 -update 1 -atomic_writing 1 -y \"{}\"", preview_name)
		);
	}

	std::string ffmpeg_command = "";
	for (const auto& ffmpeg_setting : ffmpeg_settings)
		ffmpeg_command += " " + ffmpeg_setting;

	return ffmpeg_command;
}

void c_rendering::render_videos() {
	auto& queue = rendering.queue;

	rendering.in_render = true;

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

	rendering.in_render = false;
}

void c_rendering::render_videos_thread() {
	while (true) {
		if (!queue.empty()) {
			auto render = queue.front();

			in_render = true;

			try {
				render.render();
			}
			catch (const std::exception& e) {
				console.print(e.what());
			}

			// finished rendering, delete
			queue.erase(queue.begin());

			in_render = !queue.empty();
			if (!in_render) {
				console.print("finished render queue, waiting for input.");
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
}

void c_render::build_output_filename() {
	// build output filename
	int num = 1;
	do {
		this->output_filename = fmt::format("{} - blur", this->video_name);

		if (this->settings.detailed_filenames) {
			this->output_filename += " (";
			this->output_filename += fmt::format("{}fps", this->settings.output_fps);
			if (this->settings.interpolate)
				this->output_filename += fmt::format("~{}", this->settings.interpolated_fps);
			if (this->settings.blur)
				this->output_filename += fmt::format("~{}", this->settings.blur_amount);
			this->output_filename += ")";
		}

		if (num > 1)
			this->output_filename += fmt::format(" ({})", num);

		this->output_filename += ".mp4";

		num++;
	} while (std::filesystem::exists(this->output_filename));
}

c_render::c_render(const std::string& input_path, std::optional<std::string> output_filename, std::optional<std::string> config_path) {
	this->video_path = input_path;

	this->video_name = std::filesystem::path(this->video_path).stem().string();
	this->video_folder = std::filesystem::path(this->video_path).parent_path().string() + "\\";

	this->input_filename = std::filesystem::path(this->video_path).filename().string();

	// set working directory
	std::filesystem::current_path(this->video_folder);

	if (blur.using_ui) {
		// print message
		if (!rendering.queue.empty()) {
			console.print_blank_line();
			console.print_line();
		}

		console.print(fmt::format("opening {} for processing", this->input_filename));
	}

	if (config_path.has_value()) {
		std::string config_folder = std::filesystem::path(config_path.value()).string() + "\\";
		bool first_time_config = false; // todo: remove need for these
		std::string config_filepath;
		this->settings = config.parse(config_folder, config_filepath, first_time_config);

		if (first_time_config) {
			if (blur.verbose)
				console.print(fmt::format("configuration file not found, default config generated at {}", config_filepath));
		}
	}
	else {
		// parse config file (do it now, not when rendering. nice for batch rendering the same file with different settings)
		bool first_time_config = false;
		std::string config_filepath;
		this->settings = config.parse(this->video_folder, config_filepath, first_time_config);

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
			}
		}
	}

	if (output_filename.has_value())
		this->output_filename = output_filename.value();
	else
		this->build_output_filename();

	if (blur.using_ui) {
		console.print(fmt::format("writing to {}", this->output_filename));

		console.print_line();
	}
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
		console.print(fmt::format("- source {}fps video at {:.2f} timescale -", settings.input_fps, settings.input_timescale));
		if (settings.interpolate) console.print(fmt::format("- interpolated to {}fps with {:.2f} timescale -", settings.interpolated_fps, settings.output_timescale));
		if (settings.blur) console.print(fmt::format("- motion blurred ({}%) -", static_cast<int>(settings.blur_amount * 100)));
		console.print(fmt::format("- rendered into {}fps with crf {} -", settings.output_fps, settings.crf));

		console.print_line();
	}

	// create avisynth script
	avisynth.create(temp_path, video_path, settings);

	// start preview
	if (settings.preview) {
		preview_filename = temp_path + "preview.jpg";
		preview.start(preview_filename);
	}

	// render
	auto ffmpeg_settings = rendering.get_ffmpeg_command(settings, output_filename, preview_filename);
	system(ffmpeg_settings.c_str());

	// finished render
	if (blur.verbose) {
		console.print_blank_line();
		console.print(fmt::format("finished rendering {}", video_name));
		console.print_line();
	}

	// remove temp path and files inside
	blur.remove_temp_path(temp_path);

	preview.disable();
}