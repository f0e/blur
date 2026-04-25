#pragma once

namespace waveforms {
	std::optional<std::vector<int16_t>> get_waveform(const std::filesystem::path& video_path, int target_width);
}
