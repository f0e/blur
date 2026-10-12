#pragma once

#include "config_blur.h"

namespace weighting {
	constexpr int ASSUMED_FRAME_GAP = 20;

	std::vector<double> normalize(const std::vector<double>& weights);
	std::vector<double> scale_range(int n, double start, double end);

	struct WeightingResult {
		std::vector<double> weights;
		std::string error;
	};

	WeightingResult equal(int frames);
	WeightingResult cosine(int frames);
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
	WeightingResult divide(int frames, const std::vector<double>& weights);

	// a weighting as a curve across the shutter, from 0 (opening) to 1 (closing), and where it bends
	struct Curve {
		std::function<double(double)> shape;
		std::vector<double> corners;
		std::string error;
	};

	Curve get_type_curve(const std::string& weighting, double mean, double stddev, std::pair<double, double> bound);

	// each frame's share of the curve over a shutter `exposure` frames long, centred on the output frame
	std::vector<double> integrate(double exposure, const Curve& curve, bool trapezoid);

	WeightingResult get_type_weights(
		int frames, const std::string& weighting, double mean, double stddev, std::pair<double, double> bound
	);

	std::optional<std::pair<double, double>> parse_gaussian_bound(const std::string& json_str);

	struct GetWeightsResult {
		// the frames each output frame blends, centred on it
		std::vector<double> weights;
		double frame_gap = 0.0; // source frames per output frame
		int source_fps = 0;
		bool fps_known = false;
		std::string error;
	};

	GetWeightsResult get_weights(const BlurSettings& settings, std::optional<int> video_fps);
}
