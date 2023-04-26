#include "blur.h"

#include "console.h"
#include "preview.h"
#include "gui.h"

std::vector<std::string> c_blur::get_files(int& argc, char* argv[]) {
	std::vector<std::string> video_paths;

	if (argc <= 1) {
		if (!rendering.renders_queued) {
			console::print("waiting for input, drag a video onto this window.");
		}

		// wait for the user to drop a file onto the window
		// todo
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

std::filesystem::path c_blur::create_temp_path(const std::filesystem::path& video_path) {
	std::filesystem::path temp_path = video_path / "blur_temp";

	// check if the path already exists
	if (!std::filesystem::exists(temp_path)) {
		// try create the path
		if (!std::filesystem::create_directory(temp_path))
			throw std::exception("failed to create temporary path");
	}

	helpers::set_hidden(temp_path);

	return temp_path;
}

void c_blur::remove_temp_path(const std::filesystem::path& path) {
	// check if the path doesn't already exist
	if (!std::filesystem::exists(path))
		return;

	// remove the path and all the files inside
	std::filesystem::remove_all(path);
}

void c_blur::run(int argc, char* argv[], const cxxopts::ParseResult& cmd) {
	/*
	using_ui = !cmd["noui"].as<bool>();
	verbose = using_ui || cmd["verbose"].as<bool>();
	no_preview = cmd["nopreview"].as<bool>();

	if (using_ui) {
		console::init();

		// print art
		const std::vector<const char*> art{
			"    _/        _/                   ",
			"   _/_/_/    _/  _/    _/  _/  _/_/",
			"  _/    _/  _/  _/    _/  _/_/     ",
			" _/    _/  _/  _/    _/  _/        ",
			"_/_/_/    _/    _/_/_/  _/         ",
		};

		console::print_blank_line();

		for (const auto& line : art)
			console::print(line);

		console::print_line();
	}

	path = std::filesystem::path(helpers::get_executable_path()).parent_path(); // folder the exe is in
	used_installer = std::filesystem::exists(path / "lib\\vapoursynth\\vspipe.exe")
		&& std::filesystem::exists(path / "lib\\ffmpeg\\ffmpeg.exe");

	if (!used_installer) {
		// didn't use installer, check if dependencies were installed
		try {
			if (!helpers::detect_command("ffmpeg"))
				throw std::exception("FFmpeg could not be found");

			if (!helpers::detect_command("py"))
				throw std::exception("Python could not be found");

			if (!helpers::detect_command("vspipe"))
				throw std::exception("VapourSynth could not be found");
		}
		catch (const std::exception& e) {
			console::print(e.what());
			console::print_blank_line();
			console::print("Make sure you have followed the installation instructions");
			console::print("https://github.com/f0e/blur/blob/master/README.md#Requirements");
			console::print_blank_line();
			console::print("If you're still unsure, open an issue on the GitHub");
			console::print_line();

			if (using_ui) {
				console::print("Press any key to exit.");
				_getch();
			}

			return;
		}
	}

	if (using_ui) {
		while (!GetAsyncKeyState(VK_ESCAPE)) {
			try {
				// get/wait for input file
				auto videos = get_files(argc, argv);
				for (auto& video_path : videos) {
					// check the file exists
					if (!std::filesystem::exists(video_path))
						throw std::exception("video couldn't be opened (wrong path?)\n");

					// render video
					rendering.queue_render(std::make_shared<c_render>(video_path));
				}

				// start rendering
				rendering.start_thread();
			}
			catch (const std::exception& e) {
				console::print(e.what());
				console::print_blank_line();
			}
		}

		rendering.stop_thread();
	}
	else { // command-line mode
		std::vector<std::string> inputs, outputs, config_paths;

		if (!cmd.count("input")) {
			console::print("No input files specified. Use -i or --input.");
			return;
		}

		inputs = cmd["input"].as<std::vector<std::string>>();

		bool manual_output_files = cmd.count("output");
		if (manual_output_files) {
			if (cmd.count("input") != cmd.count("output")) {
				console::print(fmt::format("Input/output filename count mismatch ({} inputs, {} outputs).", cmd.count("input"), cmd.count("output")));
				return;
			}

			outputs = cmd["output"].as<std::vector<std::string>>();
		}

		bool manual_config_files = cmd.count("config-path"); // todo: cleanup redundancy ^^
		if (manual_config_files) {
			if (cmd.count("input") != cmd.count("config-path")) {
				console::print(fmt::format("Input filename/config paths count mismatch ({} inputs, {} config paths).", cmd.count("input"), cmd.count("config-path")));
				return;
			}

			config_paths = cmd["config-path"].as<std::vector<std::string>>();

			// check if all configs exist
			bool paths_ok = true;
			for (const auto& path : config_paths) {
				if (!std::filesystem::exists(path)) {
					console::print(fmt::format("Specified config file path '{}' not found.", path));
					paths_ok = false;
				}
			}

			if (!paths_ok)
				return;
		}

		// queue videos to be rendered
		for (size_t i = 0; i < inputs.size(); i++) {
			std::filesystem::path input_path = std::filesystem::absolute(inputs[i]);

			if (!std::filesystem::exists(input_path)) {
				console::print(fmt::format("Video {} was not found (wrong path?)", input_path.string()));
				return;
			}

			std::optional<std::filesystem::path> output_path;
			std::optional<std::filesystem::path> config_path;

			if (manual_config_files)
				config_path = std::filesystem::absolute(config_paths[i]).string();

			if (manual_output_files) {
				output_path = std::filesystem::absolute(outputs[i]);

				// create output directory if needed
				if (!std::filesystem::exists(output_path->parent_path()))
					std::filesystem::create_directories(output_path->parent_path());
			}

			// set up render
			rendering.queue_render(std::make_shared<c_render>(input_path, output_path, config_path));

			if (verbose) {
				// console::print(fmt::format("Queued '{}' for render, outputting to '{}'", render.get_video_name(), render.get_output_video_path().string()));
			}
		}

		// render videos
		rendering.render_videos();
	}

	preview.stop();
	*/
}

void c_blur::run_gui() {
	path = std::filesystem::path(helpers::get_executable_path()).parent_path(); // folder the exe is in
	used_installer = std::filesystem::exists(path / "lib\\vapoursynth\\vspipe.exe")
		&& std::filesystem::exists(path / "lib\\ffmpeg\\ffmpeg.exe");

	while (gui::open) {
		rendering.render_videos();
	}

	preview.stop();
}