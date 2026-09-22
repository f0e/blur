#pragma once

namespace waveforms {
	std::optional<std::vector<int16_t>> get_waveform(const std::filesystem::path& video_path, int target_width);

	int16_t get_audio_percentile_peak(const std::vector<int16_t>& samples, float percentile);
}
