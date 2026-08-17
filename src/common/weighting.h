#pragma once

namespace weighting {
	std::vector<double> normalize(const std::vector<double>& weights);
	std::vector<double> scale_range(int n, double start, double end);

	struct WeightingResult {
		std::vector<double> weights;
		std::string error;
	};

	WeightingResult equal(int frames);
	WeightingResult ascending(int frames);
	WeightingResult descending(int frames);
	WeightingResult pyramid(int frames);
	WeightingResult gaussian(
		int frames, double mean = 2.0, double stddev = 1.0, std::pair<double, double> bound = { 0.0, 2.0 }
	);
	WeightingResult gaussian_reverse(
		int frames, double mean = 2.0, double stddev = 1.0, std::pair<double, double> bound = { 0.0, 2.0 }
	);
	WeightingResult gaussian_sym(int frames, double stddev = 1.0, std::pair<double, double> bound = { 0.0, 2.0 });
	WeightingResult vegas(int frames);
	WeightingResult divide(int frames, const std::vector<double>& weights);

	std::optional<std::pair<double, double>> parse_gaussian_bound(const std::string& json_str);

	struct GetWeightsResult {
		std::vector<double> weights;
		std::string error;
	};

	GetWeightsResult get_weights(const BlurSettings& settings, std::optional<int> video_fps);
}
