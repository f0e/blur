#pragma once

#include "config_blur.h"

namespace encoding {
	std::vector<std::string> get_available_gpu_types();
	std::string get_primary_gpu_type();

	std::vector<std::string> get_supported_encoding_presets(bool gpu_encoding, const std::string& gpu_type);
	std::vector<std::string> get_supported_encoding_presets(
		const EncodingPresetSettings& presets, bool gpu_encoding, const std::string& gpu_type
	);

	// runs the gpu and codec checks up front so their results are cached before anything needs them
	void probe_support();
	bool support_probed();

	std::vector<std::string> ffmpeg_string_to_args(const std::string& str);

	void verify_gpu_encoding(BlurSettings& settings);
}
