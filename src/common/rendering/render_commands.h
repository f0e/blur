#pragma once

#include "common/config_app.h"
#include "common/config_blur.h"
#include "common/config_encoding_presets.h"
#include "common/devices.h"
#include "common/media.h"

// turn settings and video info into vspipe/ffmpeg arguments
namespace rendering::detail {
	nlohmann::json merge_settings(
		const BlurSettings& blur_settings,
		const GlobalAppSettings& app_settings,
		const devices::DeviceIndices& device_indices
	);

	std::vector<std::string> build_vspipe_base_args(
		const std::filesystem::path& input_path, const nlohmann::json& merged_settings
	);

	std::vector<std::string> build_vspipe_video_args(
		const std::filesystem::path& input_path,
		const nlohmann::json& merged_settings,
		const media::VideoInfo& video_info,
		std::optional<size_t> start_frame = {},
		std::optional<size_t> end_frame = {},

		// separate from start_frame so a preview's auto mask doesn't follow the seek bar
		std::optional<std::pair<size_t, size_t>> mask_range = {},
		bool preview_mask = false
	);

	bool copies_audio(const BlurSettings& settings, const GlobalAppSettings& app_settings);
	bool copies_audio(
		const BlurSettings& settings, const GlobalAppSettings& app_settings, const EncodingPresetSettings& presets
	);

	tl::expected<std::filesystem::path, std::string> build_output_filename(
		const std::filesystem::path& input_path, const BlurSettings& settings, const GlobalAppSettings& app_settings
	);

	std::optional<std::string> get_audio_copy_conflict(const BlurSettings& settings, bool trimming);

	// the full ffmpeg command for a video render, up to the output path. the preview pipe is added by the caller
	tl::expected<std::vector<std::string>, std::string> build_ffmpeg_video_args(
		const std::filesystem::path& input_path,
		const media::VideoInfo& video_info,
		const BlurSettings& settings,
		const GlobalAppSettings& app_settings,
		const std::filesystem::path& output_path,
		size_t start_frame,
		size_t end_frame,
		bool trimming
	);

	std::vector<std::string> build_ffmpeg_preview_args();

	void copy_file_timestamp(const std::filesystem::path& from, const std::filesystem::path& to);
}
