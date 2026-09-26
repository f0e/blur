#pragma once

#include "config_app.h"
#include "config_blur.h"

namespace devices {
	inline std::atomic<bool> initialised = false;
	inline std::map<int, std::string> rife;
	inline std::map<int, std::string> tensorrt;

	inline std::atomic<bool> benchmarking = false;

	void initialise();

	std::optional<std::string> get_auto_rife_device();
	std::optional<std::string> get_auto_tensorrt_device();

	std::optional<bool> get_svp_gpu_supported();
	bool wait_svp_gpu_supported();

	struct DeviceIndices {
		int rife = -1;
		int tensorrt = -1;
	};

	DeviceIndices get_device_indices(const GlobalAppSettings& app_settings);
}
