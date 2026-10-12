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
		// multiplied before dividing like the python, so indices that land on whole numbers round the same way
		for (int i = 0; i < n; ++i) {
			result.push_back(start + (i * (end - start) / (n - 1)));
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

	// raised cosine (hann). at blur amount 2 neighbouring output frames' windows sum to a constant
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

namespace {
	// { node, weight } for 5 point gauss-legendre on [0, 1], exact for polynomials up to degree 9
	constexpr std::array<std::pair<double, double>, 5> NODES = { {
		{ 0.5 - (0.5 * 0.906179845938664), 0.5 * 0.2369268850561891 },
		{ 0.5 - (0.5 * 0.5384693101056831), 0.5 * 0.4786286704993665 },
		{ 0.5, 0.5 * 0.5688888888888889 },
		{ 0.5 + (0.5 * 0.5384693101056831), 0.5 * 0.4786286704993665 },
		{ 0.5 + (0.5 * 0.906179845938664), 0.5 * 0.2369268850561891 },
	} };

	std::vector<double> parse_custom_weights(const std::string& weighting) {
		std::vector<double> weights;
		std::istringstream ss(weighting);
		std::string token;

		while (std::getline(ss, token, ','))
			weights.push_back(std::stod(token));

		return weights;
	}

	std::string invalid_weighting_error(const std::string& weighting) {
		return "Invalid blur_weighting value: " + weighting +
		       ". Valid options: 'equal', 'cosine', 'gaussian_sym', 'pyramid', 'gaussian', 'ascending', 'descending', "
		       "'gaussian_reverse', or a comma-separated list (e.g. '1, 2, 3').";
	}
}

std::vector<double> weighting::integrate(double exposure, const Curve& curve, bool trapezoid) {
	double half = exposure / 2;
	double reach = trapezoid ? 1.0 : 0.5;
	int last = static_cast<int>(std::ceil(half + reach));

	std::vector<double> weights;
	for (int i = -last; i <= last; i++) {
		double start = std::max(-half, i - reach);
		double end = std::min(half, i + reach);
		if (end <= start) {
			weights.push_back(0.0);
			continue;
		}

		std::vector<double> cuts = { start, end };
		if (trapezoid && start < i && i < end)
			cuts.push_back(i);
		for (double corner : curve.corners) {
			double t = -half + (corner * exposure);
			if (start < t && t < end)
				cuts.push_back(t);
		}
		std::ranges::sort(cuts);
		cuts.erase(std::ranges::unique(cuts).begin(), cuts.end());

		double area = 0.0;
		for (size_t piece = 0; piece + 1 < cuts.size(); piece++) {
			double a = cuts[piece];
			double b = cuts[piece + 1];
			for (auto [node, node_weight] : NODES) {
				double t = a + ((b - a) * node);
				double hat = trapezoid ? 1 - std::abs(t - i) : 1.0;
				area += node_weight * (b - a) * hat * curve.shape((t + half) / exposure);
			}
		}
		weights.push_back(area);
	}

	while (weights.size() > 1 && weights.front() == 0.0 && weights.back() == 0.0) {
		weights.erase(weights.begin());
		weights.pop_back();
	}

	return normalize(weights);
}

weighting::Curve weighting::get_type_curve(
	const std::string& weighting, double mean, double stddev, std::pair<double, double> bound
) {
	if (weighting == "equal" || weighting == "vegas")
		return { .shape = [](double) {
			return 1.0;
		} };

	if (weighting == "cosine")
		return { .shape = [](double x) {
			return 1 - std::cos(2 * std::numbers::pi * x);
		} };

	if (weighting == "ascending")
		return { .shape = [](double x) {
			return x;
		} };

	if (weighting == "descending")
		return { .shape = [](double x) {
			return 1 - x;
		} };

	if (weighting == "pyramid")
		return {
			.shape =
				[](double x) {
					return 1 - std::abs((2 * x) - 1);
				},
			.corners = { 0.5 },
		};

	if (weighting == "gaussian" || weighting == "gaussian_reverse" || weighting == "gaussian_sym") {
		if (bound.first == bound.second)
			return { .error = "Gaussian bound must have two distinct values" };

		auto [start, end] = bound;
		if (weighting == "gaussian_sym") {
			end = std::max(std::abs(start), std::abs(end));
			start = -end;
			mean = 0.0;
		}

		bool reverse = weighting == "gaussian_reverse";
		return { .shape = [=](double x) {
			if (reverse)
				x = 1 - x;
			return std::exp(-std::pow(start + (x * (end - start)) - mean, 2) / (2 * stddev * stddev));
		} };
	}

	std::vector<double> steps;
	try {
		steps = parse_custom_weights(weighting);
	}
	catch (...) {
		return { .error = invalid_weighting_error(weighting) };
	}

	double lowest = *std::ranges::min_element(steps);
	if (lowest < 0.0) {
		for (double& step : steps)
			step = step - lowest + 1;
	}

	Curve curve{ .shape = [steps](double x) {
		auto count = static_cast<int>(steps.size());
		return steps[std::min(static_cast<int>(x * count), count - 1)];
	} };
	for (size_t i = 1; i < steps.size(); i++)
		curve.corners.push_back(static_cast<double>(i) / static_cast<double>(steps.size()));

	return curve;
}

weighting::WeightingResult weighting::get_type_weights(
	int frames, const std::string& weighting, double mean, double stddev, std::pair<double, double> bound
) {
	if (weighting == "equal" || weighting == "vegas")
		return equal(frames);
	if (weighting == "cosine")
		return cosine(frames);
	if (weighting == "ascending")
		return ascending(frames);
	if (weighting == "descending")
		return descending(frames);
	if (weighting == "pyramid")
		return pyramid(frames);
	if (weighting == "gaussian")
		return gaussian(frames, mean, stddev, bound);
	if (weighting == "gaussian_reverse")
		return gaussian_reverse(frames, mean, stddev, bound);
	if (weighting == "gaussian_sym")
		return gaussian_sym(frames, stddev, bound);

	try {
		return divide(frames, parse_custom_weights(weighting));
	}
	catch (...) {
		return { .error = invalid_weighting_error(weighting) };
	}
}

weighting::GetWeightsResult weighting::get_weights(const BlurSettings& settings, std::optional<int> video_fps) {
	if (!settings.blur)
		return { .weights = {} };

	if (settings.blur_amount <= 0.f)
		return { .weights = {} };

	// without the video fps, assume a fixed number of source frames per output frame so the weights still follow the
	// blur amount
	double frame_gap =
		video_fps ? static_cast<double>(*video_fps) / settings.blur_output_fps : static_cast<double>(ASSUMED_FRAME_GAP);
	double exposure = frame_gap * settings.blur_amount;

	auto gaussian_bound = parse_gaussian_bound(settings.advanced.blur_weighting_gaussian_bound);
	if (!gaussian_bound)
		return { .error = "Failed to parse gaussian bound" };

	double mean = settings.advanced.blur_weighting_gaussian_mean;
	double stddev = settings.advanced.blur_weighting_gaussian_std_dev;

	// matches weighting.parse in the render
	WeightingResult res;
	if (settings.blur_sampling == "round up" || settings.blur_sampling == "legacy") {
		auto frames = settings.blur_sampling == "round up"
		                  ? static_cast<int>(std::lround(exposure))
		                  : static_cast<int>(std::trunc(frame_gap) * settings.blur_amount);
		if (frames <= 0)
			return { .weights = {} };
		if (frames % 2 == 0)
			frames++;

		res = get_type_weights(frames, settings.blur_weighting, mean, stddev, *gaussian_bound);
	}
	else {
		auto curve = get_type_curve(settings.blur_weighting, mean, stddev, *gaussian_bound);
		if (!curve.error.empty())
			return { .error = curve.error };

		res.weights = integrate(exposure, curve, settings.blur_sampling == "trapezoid");
	}

	if (!res.error.empty())
		return { .error = res.error };

	return {
		.weights = res.weights,
		.frame_gap = frame_gap,
		.source_fps = video_fps ? *video_fps : ASSUMED_FRAME_GAP * settings.blur_output_fps,
		.fps_known = video_fps.has_value(),
	};
}
