#pragma once

namespace waveforms {
	// one per audio track
	std::optional<std::vector<std::vector<int16_t>>> get_waveforms(
		const std::filesystem::path& video_path, size_t track_count, int target_width
	);
}
