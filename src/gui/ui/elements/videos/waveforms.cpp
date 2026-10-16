#include "videos.h"
#include "common/waveforms.h"

namespace {
	constexpr size_t SAMPLES_PER_SEC = 80;

	std::unordered_map<std::string, ui::VideoWaveform> cache;
}

ui::VideoWaveform* ui::videos::get_waveform(const std::filesystem::path& path, size_t track_count, float duration) {
	if (duration <= 0.f)
		return nullptr;

	auto key = u::path_to_string(path);

	auto it = cache.find(key);
	if (it != cache.end())
		return &it->second;

	try {
		auto tracks = waveforms::get_waveforms(path, track_count, static_cast<int>(duration * SAMPLES_PER_SEC));
		if (!tracks)
			return nullptr;

		// shared between tracks so a quiet one looks quiet
		int16_t max_sample = 1;
		for (const auto& samples : *tracks) {
			for (auto sample : samples) {
				max_sample = std::max<int>(std::abs(sample), max_sample);
			}
		}

		return &cache
		            .emplace(
						key,
						VideoWaveform{
							.tracks = std::move(*tracks),
							.max_sample = max_sample,
						}
					)
		            .first->second;
	}
	catch (const std::exception& e) {
		u::log_error("failed to load waveform for {} ({})", key, e.what());
		return nullptr;
	}
}
