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
	WeightingResult cosine(int frames, bool rectangle = false);
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

	std::optional<std::pair<double, double>> parse_gaussian_bound(const std::string& json_str);

	struct GetWeightsResult {
		// the frames each output frame blends, without the zero the render pads an even rectangle count with
		std::vector<double> weights;
		double window_center = 0.0; // source frames from the output frame to the middle of those frames
		double frame_gap = 0.0;     // source frames per output frame
		int source_fps = 0;
		bool fps_known = false;
		std::string error;
	};

	GetWeightsResult get_weights(const BlurSettings& settings, std::optional<int> video_fps);
}
