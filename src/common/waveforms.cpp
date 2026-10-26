#include "waveforms.h"

namespace {
	struct Waveform {
		bool ready = false;
		std::vector<std::vector<int16_t>> tracks;
	};

	std::unordered_map<std::string, Waveform> waveform_cache;
	std::mutex waveform_mutex;

	std::vector<int16_t> ffmpeg_get_waveform_samples(
		const std::filesystem::path& path, size_t track, int target_width
	) {
		namespace bp = boost::process;

		bp::ipstream pipe_stream;

		auto c = u::run_command(
			blur.ffmpeg_path,
			{
				"-v",
				"error",
				"-i",
				u::path_to_string(path),
				"-map",
				std::format("0:a:{}", track),
				"-f",
				"s16le",
				"-acodec",
				"pcm_s16le",
				"-ac",
				"1",
				"-ar",
				"44100",
				"-",
			},
			bp::std_out > pipe_stream,
			bp::std_err.null()
		);

		if (!c) {
			u::log_error("failed to get waveform: {}", c.error());
			return {};
		}

		std::vector<char> buffer(4096);
		std::vector<int16_t> raw_samples;

		while (pipe_stream.read(buffer.data(), buffer.size()) || pipe_stream.gcount() > 0) {
			auto bytes_read = static_cast<size_t>(pipe_stream.gcount());

			if (bytes_read % 2 != 0)
				--bytes_read;

			size_t sample_count = bytes_read / 2;
			size_t old_size = raw_samples.size();
			raw_samples.resize(raw_samples.size() + sample_count);
			std::memcpy(&raw_samples[old_size], buffer.data(), bytes_read);
		}

		std::vector<int16_t> downsampled;
		if (!raw_samples.empty()) {
			size_t samples_per_pixel = std::max<size_t>(1, raw_samples.size() / target_width);

			for (size_t i = 0; i < raw_samples.size(); i += samples_per_pixel) {
				float sum = 0.0f;
				for (size_t j = i; j < std::min(i + samples_per_pixel, raw_samples.size()); ++j) {
					auto sample = static_cast<float>(raw_samples[j]);
					sum += sample * sample;
				}
				float rms = std::sqrt(sum / samples_per_pixel);
				downsampled.push_back(static_cast<int16_t>(rms));
			}
		}

		return downsampled;
	}

	void ffmpeg_get_waveforms_async(
		const std::filesystem::path& video_path, const std::string& key, size_t track_count, int target_width
	) {
		u::log("spawning waveform thread for {}", u::path_to_string(video_path));

		std::thread([video_path, key, track_count, target_width] {
			std::vector<std::vector<int16_t>> tracks;
			tracks.reserve(track_count);

			for (size_t i = 0; i < track_count; i++)
				tracks.push_back(ffmpeg_get_waveform_samples(video_path, i, target_width));

			std::unique_lock lock(waveform_mutex);
			waveform_cache[key].tracks = std::move(tracks);
			waveform_cache[key].ready = true;
		}).detach();
	}
}

std::optional<std::vector<std::vector<int16_t>>> waveforms::get_waveforms(
	const std::filesystem::path& video_path, size_t track_count, int target_width
) {
	const auto key = u::path_to_string(video_path);

	std::unique_lock lock(waveform_mutex);

	auto it = waveform_cache.find(key);

	if (it == waveform_cache.end()) {
		waveform_cache[key] = {};

		ffmpeg_get_waveforms_async(video_path, key, track_count, target_width);

		return {};
	}

	if (!it->second.ready)
		return {};

	auto tracks = std::move(it->second.tracks);

	// remove from cache now that we've fetched it
	// TODO: if its never fetched then itll sit in memory forever
	waveform_cache.erase(key);

	return tracks;
}
