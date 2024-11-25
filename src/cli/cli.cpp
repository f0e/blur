#include "cli.h"

bool cli::run(
	std::vector<std::string> inputs,
	std::vector<std::string> outputs,
	std::vector<std::string> config_paths,
	bool preview,
	bool verbose
) {
	if (!blur.initialise(verbose, preview)) {
		std::wcout << L"Blur failed to initialize" << std::endl;
		return false;
	}

	bool manual_output_files = !outputs.empty();
	if (manual_output_files && inputs.size() != outputs.size()) {
		std::wcout << L"Input/output filename count mismatch (" << inputs.size() << L" inputs, " << outputs.size()
				   << L" outputs)." << std::endl;
		return false;
	}

	bool manual_config_files = !config_paths.empty();
	if (manual_config_files && inputs.size() != config_paths.size()) {
		std::wcout << L"Input filename/config paths count mismatch (" << inputs.size() << L" inputs, "
				   << config_paths.size() << L" config paths)." << std::endl;
		return false;
	}

	if (manual_config_files) {
		for (const auto& path : config_paths) {
			if (!std::filesystem::exists(path)) {
				// TODO: test printing works with unicode
				std::cout << "Specified config file path '" << path << "' not found." << std::endl;
				return false;
			}
		}
	}

	for (size_t i = 0; i < inputs.size(); ++i) {
		std::filesystem::path input_path = std::filesystem::canonical(inputs[i]);

		if (!std::filesystem::exists(input_path)) {
			// TODO: test with unicode
			std::wcout << L"Video '" << input_path.wstring() << L"' was not found (wrong path?)" << std::endl;
			return false;
		}

		std::optional<std::filesystem::path> output_path;
		std::optional<std::filesystem::path> config_path;

		if (manual_config_files)
			config_path = std::filesystem::canonical(config_paths[i]);

		if (manual_output_files) {
			output_path = std::filesystem::canonical(outputs[i]);

			// create output directory if needed
			if (!std::filesystem::exists(output_path->parent_path()))
				std::filesystem::create_directories(output_path->parent_path());
		}

		// set up render
		auto render = std::make_shared<c_render>(input_path, output_path, config_path);

		rendering.queue_render(render);

		if (blur.verbose) {
			u::log(
				L"Queued '{}' for render, outputting to '{}'",
				render->get_video_name(),
				render->get_output_video_path().wstring()
			);
		}
	}

	// render videos
	rendering.render_videos();

	std::wcout << L"Finished rendering" << std::endl;

	return true;
}
