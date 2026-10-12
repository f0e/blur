#include "interpolation_models.h"
#include "paths.h"

#ifdef TENSORRT
namespace {
	std::vector<std::string> list_onnx(const std::filesystem::path& folder) {
		std::vector<std::string> models;

		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
			if (entry.is_regular_file(ec) && entry.path().extension() == ".onnx")
				models.push_back(u::path_to_string(entry.path().stem()));
		}

		std::ranges::sort(models);

		return models;
	}
}
#endif

std::filesystem::path interpolation_models::get_rife_path() {
#if defined(_WIN32)
	return paths::get_resources_path() / "lib/models";
#else
	return paths::get_resources_path() / "models";
#endif
}

std::vector<std::string> interpolation_models::list_rife() {
	std::vector<std::string> models;

	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(get_rife_path(), ec)) {
		if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / "flownet.param", ec))
			models.push_back(u::path_to_string(entry.path().filename()));
	}

	std::ranges::sort(models);

	return models;
}

#ifdef TENSORRT
std::filesystem::path interpolation_models::get_rife_trt_path() {
	return paths::get_resources_path() / "lib/vapoursynth/vs-plugins/models/rife_v2";
}

// gimm-vfi takes the same inputs as rife, but lives apart so it isn't listed as a rife model
std::filesystem::path interpolation_models::get_gimm_trt_path() {
	return paths::get_resources_path() / "lib/vapoursynth/vs-plugins/models/gimm_vfi";
}

std::vector<std::string> interpolation_models::list_gimm_trt() {
	return list_onnx(get_gimm_trt_path());
}

bool interpolation_models::trt_installed() {
	std::error_code ec;
	return std::filesystem::exists(paths::get_resources_path() / "lib/vapoursynth/vs-plugins/vstrt.dll", ec);
}

std::vector<std::string> interpolation_models::list_rife_trt() {
	return list_onnx(get_rife_trt_path());
}
#endif
