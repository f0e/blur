#pragma once

#include "common/config_app.h"
#include "common/config_blur.h"
#include "common/config_presets.h"

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
		size_t start_frame,
		size_t end_frame
	);

	bool copies_audio(const BlurSettings& settings, const GlobalAppSettings& app_settings);
	bool copies_audio(
		const BlurSettings& settings, const GlobalAppSettings& app_settings, const PresetSettings& presets
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

	void copy_file_timestamp(const std::filesystem::path& from, const std::filesystem::path& to);
}
