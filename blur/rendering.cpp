#include "rendering.h"

#include "includes.h"

c_rendering rendering;

bool detect_command(std::string command) {
	std::string find_ffmpeg_command = "where /q " + command;

	return system(find_ffmpeg_command.c_str()) == 0;
}

std::string c_rendering::get_ffmpeg_command(const blur_settings& settings, const std::string& output_name, const std::string& preview_name) {
	// check if ffmpeg is installed
	if (!detect_command("ffmpeg"))
		throw std::exception("FFmpeg could not be found");

	std::vector<std::string> ffmpeg_settings;
	if (settings.gpu) {
		// todo: make this actually be fast
		ffmpeg_settings = {
			"ffmpeg",
			"-loglevel error -hide_banner -stats", // only show progress
			"-hwaccel cuda", // gpu acceleration
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

void do_rendering() {
	while (true) {
		auto& queue = rendering.queue;

		if (!queue.empty()) {
			auto render = queue.front();

			try {
				render.render();
			}
			catch (const std::exception& e) {
				console.print_center(e.what());
			}

			// finished rendering, delete
			queue.erase(queue.begin());

			rendering.in_render = !queue.empty();
			if (!rendering.in_render) {
				console.print_center("finished render queue, waiting for input.");
			}
		}
		else {
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
	}
}

void c_rendering::start() {
	if (thread_ptr)
		return;

	thread_ptr = std::unique_ptr<std::thread>(new std::thread(&do_rendering));
	thread_ptr->detach();
}

void c_rendering::stop() {
	if (!thread_ptr)
		return;

	thread_ptr->join();
}

void c_rendering::queue_render(c_render render) {
	queue.push_back(render);

	rendering.in_render = true;
}

void c_render::render() {
	console.print_center(fmt::format("rendering {}...", video_name));
	console.print_blank_line();

	// create temp path
	temp_path = blur.create_temp_path(video_folder);

	console.print_center(fmt::format("render settings:"));
	console.print_center(fmt::format("- {} cores, {} threads -", settings.cpu_cores, settings.cpu_threads));
	console.print_center(fmt::format("- source {}fps video at {:.2f} timescale -", settings.input_fps, settings.input_timescale));
	if (settings.interpolate) console.print_center(fmt::format("- interpolated to {}fps with {:.2f} timescale -", settings.interpolated_fps, settings.output_timescale));
	if (settings.blur) console.print_center(fmt::format("- motion blurred ({}%) -", static_cast<int>(settings.blur_amount * 100)));
	console.print_center(fmt::format("- rendered into {}fps with crf {} -", settings.output_fps, settings.crf));

	console.print_line();

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

	// finished render //
	console.print_blank_line();
	console.print_center(fmt::format("finished rendering {}", video_name));
	console.print_line();

	// remove temp path and files inside
	blur.remove_temp_path(temp_path);

	preview.disable();
}