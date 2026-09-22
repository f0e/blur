#pragma once

namespace media {
	struct VideoInfo {
		bool has_video_stream = false;
		std::optional<std::string> color_range;
		std::optional<std::string> pix_fmt;
		std::optional<std::string> color_space;
		std::optional<std::string> color_transfer;
		std::optional<std::string> color_primaries;
		int sample_rate = -1;
		int fps_num = -1;
		int fps_den = -1;
		float duration = 0.f;
		int width = -1;
		int height = -1;

		std::vector<int> audio_sample_rates;

		double video_start_time = 0.0;
		std::vector<double> audio_start_times;

		int preroll_frames = 0;

		bool operator==(const VideoInfo& other) const = default;
	};

	VideoInfo get_video_info(const std::filesystem::path& path);

	// grab a single frame from the video as a jpeg, straight from the source video (no blur pipeline).
	// fast enough to use while scrubbing
	std::vector<uint8_t> get_video_frame_jpeg(const std::filesystem::path& path, float timestamp);
}
