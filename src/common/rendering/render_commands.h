#pragma once

#include "common/config_app.h"
#include "common/config_blur.h"
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

	// frames of the source
	struct FrameRange {
		size_t start = 0;
		size_t end = 0;
	};

	struct VspipeVideoOptions {
		// the whole video if it's not set
		std::optional<FrameRange> range;

		// separate from range so a preview's auto mask doesn't follow the seek bar
		std::optional<FrameRange> mask_range;

		bool preview_mask = false;
		size_t skipped_frames = 0;
	};

	std::vector<std::string> build_vspipe_video_args(
		const std::filesystem::path& input_path,
		const nlohmann::json& merged_settings,
		const media::VideoInfo& video_info,
		const VspipeVideoOptions& options = {}
	);

	// the full ffmpeg command for a video render, up to the output path. the preview pipe is added by the caller
	tl::expected<std::vector<std::string>, std::string> build_ffmpeg_video_args(
		const std::filesystem::path& input_path,
		const media::VideoInfo& video_info,
		const BlurSettings& settings,
		const GlobalAppSettings& app_settings,
		const std::filesystem::path& output_path,
		FrameRange range,
		bool trimming
	);

	tl::expected<std::vector<std::string>, std::string> build_ffmpeg_sample_args(
		const std::filesystem::path& input_path,
		const media::VideoInfo& video_info,
		const BlurSettings& settings,
		const GlobalAppSettings& app_settings,
		const std::filesystem::path& output_path,
		FrameRange range,
		size_t skipped_frames
	);

	std::vector<std::string> build_ffmpeg_preview_args();

	void copy_file_timestamp(const std::filesystem::path& from, const std::filesystem::path& to);
}
