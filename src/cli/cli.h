#pragma once

namespace cli {
	bool run(
		std::vector<std::filesystem::path> inputs,
		std::vector<std::filesystem::path> outputs,
		std::vector<std::filesystem::path> config_paths,
		bool preview,
		bool verbose,
		bool disable_update_check = false
	);
}
