#include "cli.h"
#include <common/rendering.h>

bool cli::run(
	std::vector<std::string> inputs,
	std::vector<std::string> outputs,
	std::vector<std::string> config_paths,
	bool preview,
	bool verbose
) {
	if (!blur.initialise(verbose, preview)) { // todo: preview in cli
		u::log(L"Blur failed to initialize");
		return false;
	}

	bool manual_output_files = !outputs.empty();
	if (manual_output_files && inputs.size() != outputs.size()) {
		u::log(L"Input/output filename count mismatch ({} inputs, {} outputs).", inputs.size(), outputs.size());
		return false;
	}

	bool manual_config_files = !config_paths.empty();
	if (manual_config_files && inputs.size() != config_paths.size()) {
		u::log(
			L"Input filename/config paths count mismatch ({} inputs, {} config paths).",
			inputs.size(),
			config_paths.size()
		);
		return false;
	}

	if (manual_config_files) {
		for (const auto& path : config_paths) {
			if (!std::filesystem::exists(path)) {
				// TODO: test printing works with unicode
				u::log("Specified config file path '{}' not found.", path);
				return false;
			}
		}
	}

	for (size_t i = 0; i < inputs.size(); ++i) {
		std::filesystem::path input_path = std::filesystem::canonical(inputs[i]);

		if (!std::filesystem::exists(input_path)) {
			// TODO: test with unicode
			u::log(L"Video '{}' was not found (wrong path?)", input_path.wstring());
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
		auto render = rendering.queue_render(Render(input_path, output_path, config_path));

		if (blur.verbose) {
			u::log(
				L"Queued '{}' for render, outputting to '{}'",
				render.get_video_name(),
				render.get_output_video_path().wstring()
			);
		}
	}

	// render videos
	rendering.render_videos();

	u::log(L"Finished rendering");

	return true;
}
