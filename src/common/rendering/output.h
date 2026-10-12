#pragma once

#include "common/config_app.h"
#include "common/config_blur.h"
#include "common/media.h"

struct EncodingPresetSettings;

// what a render's output is, apart from the commands that make it
namespace rendering {
	// the saved presets are used unless others are given
	std::vector<std::string> build_encoding_args(
		const BlurSettings& settings, const GlobalAppSettings& app_settings, const EncodingPresetSettings* presets
	);

	// what a full render would encode with, failing where it would. it's taken as untrimmed
	tl::expected<std::vector<std::string>, std::string> build_output_encoding_args(
		const media::VideoInfo& video_info, const BlurSettings& settings, const GlobalAppSettings& app_settings
	);

	bool wants_audio_copy(const std::vector<std::string>& encoding_args);

	bool copies_audio(const BlurSettings& settings, const GlobalAppSettings& app_settings);
	bool copies_audio(
		const BlurSettings& settings, const GlobalAppSettings& app_settings, const EncodingPresetSettings& presets
	);

	std::optional<std::string> get_audio_copy_conflict(const BlurSettings& settings, bool trimming);

	size_t get_skipped_frames(
		const BlurSettings& settings, const GlobalAppSettings& app_settings, const media::VideoInfo& video_info
	);

	// creates the output folder if it's missing
	tl::expected<std::filesystem::path, std::string> build_output_filename(
		const std::filesystem::path& input_path, const BlurSettings& settings, const GlobalAppSettings& app_settings
	);
}
