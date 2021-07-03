#include "includes.h"

std::vector<std::string> c_blur::get_files(int& argc, char* argv[]) {
	std::vector<std::string> video_paths;

	if (argc <= 1) {
		if (!rendering.in_render) {
			console.print("waiting for input, drag a video onto this window.");
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

void c_blur::remove_temp_path(const std::string& path) {
	// check if the path doesn't already exist
	if (!std::filesystem::exists(path))
		return;

	// remove the path and all the files inside
	std::filesystem::remove_all(path);
}

void c_blur::run(int argc, char* argv[], const cxxopts::ParseResult& cmd) {
	blur.using_ui = !cmd["disable-ui"].as<bool>();

	if (blur.using_ui) {
		// setup console
		console.setup();

		// print art
		const std::vector<const char*> art{
			"    _/        _/                   ",
			"   _/_/_/    _/  _/    _/  _/  _/_/",
			"  _/    _/  _/  _/    _/  _/_/     ",
			" _/    _/  _/  _/    _/  _/        ",
			"_/_/_/    _/    _/_/_/  _/         ",
		};

		console.print_blank_line();

		for (const auto& line : art)
			console.print(line);

		console.print_line();
	}

	if (using_ui) {
		try {
			while (!GetAsyncKeyState(VK_ESCAPE)) {
				// get/wait for input file
				auto videos = get_files(argc, argv);
				for (auto& video_path : videos) {
					// check the file exists
					if (!std::filesystem::exists(video_path))
						throw std::exception("video couldn't be opened (wrong path?)\n");
				
					// render video
					c_render render(video_path);
					rendering.queue_render(render);
				}
			
				// start rendering
				rendering.start_thread();
			}
		}
		catch (const std::exception& e) {
			console.print(e.what());
			console.print_blank_line();
		}

		rendering.stop_thread();
	}
	else { // command-line mode
		std::vector<std::string> input_filenames, output_filenames;

		if (!cmd.count("input")) {
			std::cout << "No input files specified. Use -i or --input." << "\n";
			return;
		}

		input_filenames = cmd["input"].as<std::vector<std::string>>();

		// check inputs exist
		for (const auto& input_filename : input_filenames) {
			if (!std::filesystem::exists(input_filename)) {
				std::cout << "Video '" << input_filename << "' was not found (wrong path?)" << "\n";
				return;
			}
		}

		bool manual_output_files = cmd.count("output");
		if (manual_output_files) {
			if (cmd.count("input") != cmd.count("output")) {
				std::cout << "Input/output filename count mismatch (" << cmd.count("input") << " inputs, " << cmd.count("output") <<
					" outputs). Use -o or --output to specify output filenames." << "\n";

				return;
			}

			output_filenames = cmd["output"].as<std::vector<std::string>>();
		}

		verbose = cmd["verbose"].as<bool>();

		std::optional<std::string> config_path;
		if (cmd.count("config-path")) {
			config_path = cmd["config-path"].as<std::string>();

			if (!std::filesystem::exists(config_path.value())) {
				std::cout << "Specified config file '" << config_path.value() << "' not found." << "\n";
				return;
			}
		}

		// queue videos to be rendered
		for (size_t i = 0; i < input_filenames.size(); i++) {
			const std::string& input_filename = input_filenames[i];
			std::optional<std::string> output_filename;

			if (manual_output_files)
				output_filename = output_filenames[i];

			// set up render
			c_render render(input_filename, output_filename, config_path);
			rendering.queue_render(render);

			if (verbose) {
				std::cout << "Queued '" << render.get_video_name() << "' for render, outputting to '" << render.get_output_video_path() << "'\n";
			}
		}

		// render videos
		rendering.render_videos();
	}

	preview.stop();
}