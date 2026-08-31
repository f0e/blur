#pragma once

#include "common/config_app.h"
#include "common/config_blur.h"
#include "common/config_encoding_presets.h"

// Pure functions that turn settings + video info into the vspipe / ffmpeg
// argument vectors. No process spawning, no shared state - just string building.
namespace rendering::detail {
	tl::expected<nlohmann::json, std::string> merge_settings(
		const BlurSettings& blur_settings, const GlobalAppSettings& app_settings
	);

	// the vspipe args common to frame and video renders
	std::vector<std::string> build_vspipe_base_args(
		const std::filesystem::path& input_path, const nlohmann::json& merged_settings
	);

	// base args plus the fps / colour range / cut-point args a video render needs
	std::vector<std::string> build_vspipe_video_args(
		const std::filesystem::path& input_path,
		const nlohmann::json& merged_settings,
		const u::VideoInfo& video_info,
		std::optional<size_t> start_frame = {},
		std::optional<size_t> end_frame = {},

		// the frame range an automatic mask should be worked out from, when that isn't just the whole video.
	    // this is deliberately separate from start_frame: a render's start_frame is the user's trim, but a
	    // preview's is wherever the seek bar happens to be, and the mask shouldn't follow the seek bar
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

	// the full ffmpeg command for a video render (audio filters, colour fixes and
	// encoding args included) up to and including the output path. The preview
	// pipe, which also toggles render state, is appended by the caller.
	tl::expected<std::vector<std::string>, std::string> build_ffmpeg_video_args(
		const std::filesystem::path& input_path,
		const u::VideoInfo& video_info,
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
