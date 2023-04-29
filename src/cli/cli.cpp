#include "cli.h"

#include <common/preview.h>

void cli::run(const cxxopts::ParseResult& cmd) {
	if (!blur.initialise(
		cmd["verbose"].as<bool>(),
		cmd["preview"].as<bool>()
	)) {
		printf("Blur failed to initialise\n");
		return;
	}

	std::vector<std::string> inputs, outputs, config_paths;

	if (!cmd.count("input")) {
		printf("No input files specified. Use -i or --input.");
		return;
	}

	inputs = cmd["input"].as<std::vector<std::string>>();

	bool manual_output_files = cmd.count("output");
	if (manual_output_files) {
		if (cmd.count("input") != cmd.count("output")) {
			printf("Input/output filename count mismatch (%zu inputs, %zu outputs).", cmd.count("input"), cmd.count("output"));
			return;
		}

		outputs = cmd["output"].as<std::vector<std::string>>();
	}

	bool manual_config_files = cmd.count("config-path"); // todo: cleanup redundancy ^^
	if (manual_config_files) {
		if (cmd.count("input") != cmd.count("config-path")) {
			printf("Input filename/config paths count mismatch (%zu inputs, %zu config paths).", cmd.count("input"), cmd.count("config-path"));
			return;
		}

		config_paths = cmd["config-path"].as<std::vector<std::string>>();

		// check if all configs exist
		bool paths_ok = true;
		for (const auto& path : config_paths) {
			if (!std::filesystem::exists(path)) {
				printf("Specified config file path '%s' not found.", path.c_str());
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
			wprintf(L"Video '%ls' was not found (wrong path?)", input_path.wstring().c_str());
			return;
		}

		std::optional<std::filesystem::path> output_path;
		std::optional<std::filesystem::path> config_path;

		if (manual_config_files)
			config_path = std::filesystem::absolute(config_paths[i]).wstring();

		if (manual_output_files) {
			output_path = std::filesystem::absolute(outputs[i]);

			// create output directory if needed
			if (!std::filesystem::exists(output_path->parent_path()))
				std::filesystem::create_directories(output_path->parent_path());
		}

		// set up render
		auto render = std::make_shared<c_render>(input_path, output_path, config_path);
		rendering.queue_render(render);

		if (blur.verbose) {
			wprintf(L"Queued '%s' for render, outputting to '%s'\n", render->get_video_name().c_str(), render->get_output_video_path().wstring().c_str());
		}
	}

	// render videos
	rendering.render_videos();

	preview.stop();
}