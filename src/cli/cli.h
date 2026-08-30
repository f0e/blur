#pragma once

namespace cli {
	bool run(
		std::vector<std::filesystem::path> inputs,
		std::vector<std::filesystem::path> outputs,
		std::vector<std::filesystem::path> config_paths,
		bool preview,
		bool verbose,
		bool disable_update_check = false,

		// masks to render every input with. left unset, each config's own masks apply
		const std::string& mask = "",
		const std::optional<bool>& auto_mask = {}
	);
}
