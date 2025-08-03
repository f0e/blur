#include "cli.h"
#include "common/rendering.h"

bool cli::run(
	std::vector<std::string> inputs,
	std::vector<std::string> outputs,
	std::vector<std::string> config_paths,
	bool preview,
	bool verbose,
	bool disable_update_check
) {
	auto init_res = blur.initialise(verbose, preview);
	if (!init_res) { // todo: preview in cli
		u::log("Blur failed to initialise");
		u::log("Reason: {}", init_res.error());
		return false;
	}

	if (!disable_update_check) {
		auto update_res = Blur::check_updates();
		if (update_res && !update_res->is_latest) {
			u::log("There's a newer version ({}) available at {}!", update_res->latest_tag, update_res->latest_tag_url);
		}
	}

	bool manual_output_files = !outputs.empty();
	if (manual_output_files && inputs.size() != outputs.size()) {
		u::log("Input/output filename count mismatch ({} inputs, {} outputs).", inputs.size(), outputs.size());
		return false;
	}

	bool manual_config_files = !config_paths.empty();
	if (manual_config_files && inputs.size() != config_paths.size()) {
		u::log(
			"Input filename/config paths count mismatch ({} inputs, {} config paths).",
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
		std::filesystem::path input_path(inputs[i]);

		if (!std::filesystem::exists(input_path)) {
			// TODO: test with unicode
			u::log("Video '{}' was not found (wrong path?)", input_path.string());
			continue;
		}

		input_path = std::filesystem::canonical(input_path);

		auto video_info = u::get_video_info(input_path);
		if (!video_info.has_video_stream) {
			u::log("Video '{}' is not a valid video or is unreadable", input_path.string());
			continue;
		}

		std::optional<std::filesystem::path> output_path;
		std::optional<std::filesystem::path> config_path;

		if (manual_config_files)
			config_path = std::filesystem::canonical(config_paths[i]);

		if (manual_output_files) {
			output_path = std::filesystem::absolute(outputs[i]);

			// create output directory if needed
			if (!std::filesystem::exists(output_path->parent_path()))
				std::filesystem::create_directories(output_path->parent_path());
		}

		// set up render
		auto render = rendering.queue_render(Render(input_path, video_info, output_path, config_path));

		if (blur.verbose) {
			u::log(
				"Queued '{}' for render, outputting to '{}'",
				render.get_video_name(),
				render.get_output_video_path().string()
			);
		}
	}

	// render videos
	while (!blur.exiting && rendering.render_next_video())
		;

	u::log("Finished rendering");

	return true;
}
