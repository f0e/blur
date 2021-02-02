#include "blur.h"

#include "includes.h"

c_blur blur;

std::vector<std::string> c_blur::get_files(int& argc, char* argv[]) {
	std::vector<std::string> video_paths;

	if (argc <= 1) {
		if (!rendering.in_render) {
			console.print_center("waiting for input, drag a video onto this window.");
		}

		// wait for the user to drop a file onto the window
		while (video_paths.empty()) {
			std::string dropped_file = console.wait_for_dropped_file();

			if (dropped_file.length() > 1)
				video_paths.push_back(dropped_file);
		}
	}
	else {
		// dropped files on exe
		int num_files = argc;
		for (int i = 1; i < num_files; i++) {
			video_paths.push_back(argv[i]);
			argc--;
		}
	}

	return video_paths;
}

std::string c_blur::create_temp_path(const std::string& video_path) {
	std::string temp_path = video_path + "/blur_temp/";

	// check if the path already exists
	if (!std::filesystem::exists(temp_path)) {
		// try create the path
		if (!std::filesystem::create_directory(temp_path))
			throw std::exception("failed to create temporary path");
	}

	// set folder as hidden
	int attr = GetFileAttributesA(temp_path.c_str());
	SetFileAttributesA(temp_path.c_str(), attr | FILE_ATTRIBUTE_HIDDEN);

	return temp_path;
}

void c_blur::remove_temp_path(std::string_view path) {
	// check if the path doesn't already exist
	if (!std::filesystem::exists(path))
		return;

	// remove the path and all the files inside
	std::filesystem::remove_all(path);
}

void c_blur::run(int argc, char* argv[]) {
	while (!GetAsyncKeyState(VK_ESCAPE)) {
		try {
			// get/wait for input file
			auto videos = get_files(argc, argv);
			for (auto& video_path : videos) {
				// check the file exists
				if (!std::filesystem::exists(video_path))
					throw std::exception("video couldn't be opened (wrong path?)\n");

				// set up render
				c_render render;
				render.video_path = video_path;
				render.video_name = std::filesystem::path(video_path).stem().string();
				render.video_folder = std::filesystem::path(video_path).parent_path().string() + "\\";

				render.input_filename = std::filesystem::path(video_path).filename().string();
				render.output_filename = render.video_name + " - blur.mp4";

				// set working directory
				std::filesystem::current_path(render.video_folder);

				// print message
				if (!rendering.queue.empty()) {
					console.print_blank_line();
					console.print_line();
					console.print_center(fmt::format("adding {} to render queue...", render.input_filename));
					console.print_center(fmt::format("writing to {}", render.output_filename));
				}
				else {
					console.print_center(fmt::format("opening {} for processing,", render.input_filename));
					console.print_center(fmt::format("writing to {}", render.output_filename));
				}

				console.print_line();

				// check if the output path already contains a video
				if (std::filesystem::exists(render.output_filename)) {
					console.print_center("destination file already exists, overwrite? (y/n)");
					console.print_blank_line();

					char choice = console.get_char();
					if (tolower(choice) == 'y') {
						std::filesystem::remove(render.output_filename);

						console.print_center("overwriting file");
						console.print_blank_line();
					}
					else {
						throw std::exception("not overwriting file");
					}
				}

				// check if the config exists
				bool first_time_config = false;
				std::string config_filepath;
				config.parse(render.video_folder, first_time_config, config_filepath);

				if (first_time_config) {
					console.print_center(fmt::format("configuration file not found, default config generated at {}", config_filepath));
					console.print_blank_line();
					console.print_center("continue render? (y/n)");
					console.print_blank_line();

					char choice = console.get_char();
					if (tolower(choice) != 'y') {
						throw std::exception("stopping render");
					}
				}

				rendering.queue_render(render);

				// start rendering
				rendering.start();
			}
		}
		catch (const std::exception& e) {
			console.print_center(e.what());
			console.print_blank_line();
		}
	}

	preview.stop();
	rendering.stop();
}