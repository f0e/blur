#pragma once

namespace rife_models {
	std::filesystem::path get_path();

	std::vector<std::string> list();

#ifdef TENSORRT
	std::filesystem::path get_trt_path();

	std::vector<std::string> list_trt();

	std::filesystem::path get_gimm_trt_path();

	bool trt_installed();
#endif
}
