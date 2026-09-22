#pragma once

namespace vspipe {
	boost::process::environment setup_environment();

	std::vector<std::string> get_args(
		const std::vector<std::string>& vspipe_flags,
		const std::string& script,
		const std::vector<std::string>& script_args,
		const std::string& output
	);
}
