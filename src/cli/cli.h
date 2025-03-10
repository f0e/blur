#pragma once

namespace cli {
	bool run(
		std::vector<std::string> inputs,
		std::vector<std::string> outputs,
		std::vector<std::string> config_paths,
		bool preview,
		bool verbose
	);
}
