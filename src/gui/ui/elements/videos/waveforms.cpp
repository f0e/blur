#include "videos.h"
#include "common/waveforms.h"

namespace {
	constexpr size_t SAMPLES_PER_SEC = 80;

	std::unordered_map<std::string, ui::VideoWaveform> cache;
}

ui::VideoWaveform* ui::videos::get_waveform(const std::filesystem::path& path, float duration) {
	if (duration <= 0.f)
		return nullptr;

	auto key = path.string();

	auto it = cache.find(key);
	if (it != cache.end())
		return &it->second;

	try {
		auto samples = waveforms::get_waveform(path, static_cast<int>(duration * SAMPLES_PER_SEC));
		if (!samples)
			return nullptr;

		int16_t max_sample = 1;
		for (auto sample : *samples) {
			max_sample = std::max<int>(std::abs(sample), max_sample);
		}

		return &cache
		            .emplace(
						key,
						VideoWaveform{
							.samples = std::move(*samples),
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
