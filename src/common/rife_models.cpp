#include "rife_models.h"
#include "paths.h"

std::filesystem::path rife_models::get_path() {
#if defined(_WIN32)
	return paths::get_resources_path() / "lib/models";
#else
	return paths::get_resources_path() / "models";
#endif
}

std::vector<std::string> rife_models::list() {
	std::vector<std::string> models;

	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(get_path(), ec)) {
		if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / "flownet.param", ec))
			models.push_back(u::path_to_string(entry.path().filename()));
	}

	std::ranges::sort(models);

	return models;
}

#ifdef TENSORRT
std::filesystem::path rife_models::get_trt_path() {
	return paths::get_resources_path() / "lib/vapoursynth/vs-plugins/models/rife_v2";
}

bool rife_models::trt_installed() {
	std::error_code ec;
	return std::filesystem::exists(paths::get_resources_path() / "lib/vapoursynth/vs-plugins/vstrt.dll", ec);
}

std::vector<std::string> rife_models::list_trt() {
	std::vector<std::string> models;

	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(get_trt_path(), ec)) {
		if (entry.is_regular_file(ec) && entry.path().extension() == ".onnx")
			models.push_back(u::path_to_string(entry.path().stem()));
	}

	std::ranges::sort(models);

	return models;
}
#endif
