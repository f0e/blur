#pragma once

namespace rife_models {
	std::filesystem::path get_path();

	std::vector<std::string> list();

#ifdef TENSORRT
	std::filesystem::path get_trt_path();

	std::vector<std::string> list_trt();

	bool trt_installed();
#endif
}
