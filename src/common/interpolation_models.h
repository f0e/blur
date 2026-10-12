#pragma once

namespace interpolation_models {
	std::filesystem::path get_rife_path();

	std::vector<std::string> list_rife();

#ifdef TENSORRT
	std::filesystem::path get_rife_trt_path();

	std::vector<std::string> list_rife_trt();

	std::filesystem::path get_gimm_trt_path();

	std::vector<std::string> list_gimm_trt();

	bool trt_installed();
#endif
}
