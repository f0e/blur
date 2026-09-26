#pragma once

namespace cli {
	bool run(
		std::vector<std::filesystem::path> inputs,
		std::vector<std::filesystem::path> outputs,
		std::vector<std::filesystem::path> config_paths,
		bool preview,
		bool verbose,
		bool disable_update_check = false,

		// unset to use each config's own masks
		const std::string& mask = "",
		const std::optional<bool>& auto_mask = {},

		// one per input, like config_paths
		const std::vector<std::string>& config_names = {}
	);
}
