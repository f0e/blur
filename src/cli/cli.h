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
		const std::optional<bool>& auto_mask = {},

		// named config per input, matched to `inputs` positionally the way `config_paths` is. empty to
	    // let each input resolve its own config, which today means the default one
		const std::vector<std::string>& config_names = {}
	);
}
