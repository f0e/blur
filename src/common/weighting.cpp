#include "weighting.h"

#include <numbers>

// thanks to:
// https://github.com/couleur-tweak-tips/smoothie-rs/blob/f1681afe3629bd7b8e3d12a109c89a8e00873248/target/scripts/weighting.py
// https://github.com/dwiandhikaap/HFR-Resampler/blob/d8f4adddc554161ebd4ebb04ca804eb046036c08/Weights.py

// TODO: should i remove the python stuff now? i like calculating it in there though still...

std::vector<double> weighting::normalize(const std::vector<double>& weights) {
	std::vector<double> adjusted = weights;
	auto min_it = std::ranges::min_element(adjusted);

	if (*min_it < 0.0) {
		double shift = -*min_it + 1.0;
		std::ranges::transform(adjusted, adjusted.begin(), [shift](double w) {
			return w + shift;
		});
	}

	double total = std::accumulate(adjusted.begin(), adjusted.end(), 0.0);
	std::ranges::transform(adjusted, adjusted.begin(), [total](double w) {
		return w / total;
	});

	return adjusted;
}

std::vector<double> weighting::scale_range(int n, double start, double end) {
	std::vector<double> result;
	if (n <= 1) {
		result.assign(n, start);
	}
	else {
		result.reserve(n);
		double step = (end - start) / (n - 1);
		for (int i = 0; i < n; ++i) {
			result.push_back(start + (i * step));
		}
	}
	return result;
}

weighting::WeightingResult weighting::equal(int frames) {
	return { .weights = std::vector<double>(frames, 1.0 / frames) };
}

weighting::WeightingResult weighting::cosine(int frames) {
	if (frames <= 1)
		return { .weights = std::vector<double>(frames, 1.0) };

	// raised cosine (hann), zero at both ends. at blur amount 2 neighbouring output frames' windows sum to a constant
	std::vector<double> weights(frames);
	for (int i = 0; i < frames; ++i)
		weights[i] = 1 - std::cos(2 * std::numbers::pi * i / (frames - 1));
	return { .weights = normalize(weights) };
}

weighting::WeightingResult weighting::ascending(int frames) {
	std::vector<double> raw(frames);
	for (int i = 0; i < frames; ++i)
		raw[i] = i + 1;
	return { .weights = normalize(raw) };
}

weighting::WeightingResult weighting::descending(int frames) {
	std::vector<double> raw(frames);
	for (int i = 0; i < frames; ++i)
		raw[i] = frames - i;
	return { .weights = normalize(raw) };
}

weighting::WeightingResult weighting::pyramid(int frames) {
	double half = (frames - 1) / 2.0;
	std::vector<double> weights(frames);
	for (int i = 0; i < frames; ++i)
		weights[i] = half - std::abs(i - half) + 1;
	return { .weights = normalize(weights) };
}

weighting::WeightingResult weighting::gaussian(
	int frames, double mean, double stddev, std::pair<double, double> bound
) {
	if (bound.first == bound.second)
		return { .error = "Gaussian bound must have two distinct values" };

	std::vector<double> x_vals = scale_range(frames, bound.first, bound.second);
	std::vector<double> weights(frames);
	double denom = 2 * stddev * stddev;

	for (int i = 0; i < frames; ++i)
		weights[i] = std::exp(-std::pow(x_vals[i] - mean, 2) / denom);

	return { .weights = normalize(weights) };
}

weighting::WeightingResult weighting::gaussian_reverse(
	int frames, double mean, double stddev, std::pair<double, double> bound
) {
	auto res = gaussian(frames, mean, stddev, bound);
	std::ranges::reverse(res.weights);
	return res;
}

weighting::WeightingResult weighting::gaussian_sym(int frames, double stddev, std::pair<double, double> bound) {
	double max_abs = std::max(std::abs(bound.first), std::abs(bound.second));
	return gaussian(frames, 0.0, stddev, { -max_abs, max_abs });
}

weighting::WeightingResult weighting::divide(int frames, const std::vector<double>& weights) {
	std::vector<double> stretched;
	std::vector<double> indices = scale_range(frames, 0, static_cast<double>(weights.size()) - 0.1);
	stretched.reserve(frames);
	for (double idx : indices)
		stretched.push_back(weights[static_cast<int>(idx)]);
	return { .weights = normalize(stretched) };
}

std::optional<std::pair<double, double>> weighting::parse_gaussian_bound(const std::string& json_str) {
	nlohmann::json json;

	auto parse_result = nlohmann::json::parse(json_str, nullptr, false);
	if (parse_result.is_discarded()) {
		return std::nullopt;
	}
	json = parse_result;

	if (!json.is_array() || json.size() != 2) {
		return std::nullopt;
	}

	if (!json[0].is_number() || !json[1].is_number()) {
		return std::nullopt;
	}

	return std::make_pair(json[0].get<double>(), json[1].get<double>());
}

weighting::GetWeightsResult weighting::get_weights(const BlurSettings& settings, std::optional<int> video_fps) {
	if (!settings.blur)
		return { .weights = {} };

	if (settings.blur_amount <= 0.f)
		return { .weights = {} };

	int blended_frames =
		30; // always get weights assuming 30 blended frames when no video fps is available for consistency

	if (video_fps) {
		int frame_gap = *video_fps / settings.blur_output_fps;
		blended_frames = frame_gap * settings.blur_amount;
		if (blended_frames <= 0)
			return { .weights = {} };
	}

	// weights must be centred on a frame, so an even count gets one more frame and its two ends are shared with the
	// neighbouring output frames. halving them means every source frame counts once in total, matching the render
	bool shared_ends = blended_frames % 2 == 0;
	if (shared_ends)
		blended_frames++;

	auto gaussian_bound = parse_gaussian_bound(settings.advanced.blur_weighting_gaussian_bound);
	if (!gaussian_bound)
		return { .error = "Failed to parse gaussian bound" };

	const std::unordered_map<std::string, std::function<WeightingResult()>> weighting_map = {
		{ "equal",
		  [&] {
			  return equal(blended_frames);
		  } },
		{ "cosine",
		  [&] {
			  return cosine(blended_frames);
		  } },
		{ "ascending",
		  [&] {
			  return ascending(blended_frames);
		  } },
		{ "descending",
		  [&] {
			  return descending(blended_frames);
		  } },
		{ "pyramid",
		  [&] {
			  return pyramid(blended_frames);
		  } },
		{ "gaussian",
		  [&] {
			  return gaussian(
				  blended_frames,
				  settings.advanced.blur_weighting_gaussian_mean,
				  settings.advanced.blur_weighting_gaussian_std_dev,
				  *gaussian_bound
			  );
		  } },
		{ "gaussian_reverse",
		  [&] {
			  return gaussian_reverse(
				  blended_frames,
				  settings.advanced.blur_weighting_gaussian_mean,
				  settings.advanced.blur_weighting_gaussian_std_dev,
				  *gaussian_bound
			  );
		  } },
		{ "gaussian_sym",
		  [&] {
			  return gaussian_sym(blended_frames, settings.advanced.blur_weighting_gaussian_std_dev, *gaussian_bound);
		  } },
		// vegas was equal with the shared ends halved, which every weighting does now
		{ "vegas",
		  [&] {
			  return equal(blended_frames);
		  } }
	};

	WeightingResult res;

	auto it = weighting_map.find(settings.blur_weighting);
	if (it != weighting_map.end()) {
		res = it->second();
	}
	else {
		try {
			std::vector<double> custom_weights;
			std::istringstream ss(settings.blur_weighting);
			std::string token;

			while (std::getline(ss, token, ',')) {
				custom_weights.push_back(std::stod(token));
			}

			res = divide(blended_frames, custom_weights);
		}
		catch (...) {
			throw std::runtime_error(
				"Invalid blur_weighting value: " + settings.blur_weighting +
				". Valid options: 'equal', 'cosine', 'gaussian_sym', 'pyramid', 'gaussian', 'ascending', 'descending', 'gaussian_reverse', or a comma-separated list (e.g. '1, 2, 3')."
			);
		}
	}

	if (!res.error.empty())
		return { .error = res.error };

	if (shared_ends) {
		res.weights.front() /= 2;
		res.weights.back() /= 2;
		res.weights = normalize(res.weights);
	}

	return { .weights = res.weights };
}
