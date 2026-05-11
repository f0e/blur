#pragma once

#include <optional>
#include <cmath>

namespace video::frame_snap {
	[[nodiscard]] inline double frame_duration(double fps) {
		if (fps <= 0.0)
			return 0.0;

		return 1.0 / fps;
	}

	[[nodiscard]] inline double snap_time(double time, double fps) {
		if (fps <= 0.0)
			return time;

		double fd = frame_duration(fps);

		return std::round(time / fd) * fd;
	}

	[[nodiscard]] inline float snap_percent(float percent, double duration, double fps) {
		if (fps <= 0.0 || duration <= 0.0)
			return percent;

		double time = percent * duration;
		double snapped_time = snap_time(time, fps);

		return static_cast<float>(snapped_time / duration);
	}

	[[nodiscard]] inline float time_to_frame(double time, double fps) {
		if (fps <= 0.0)
			return 0;

		return static_cast<int64_t>(std::round(time * fps));
	}

	[[nodiscard]] inline float frame_to_time(int64_t frame, double fps) {
		if (fps <= 0.0)
			return 0.0;

		return static_cast<double>(frame) / fps;
	}
} // namespace video::frame_snap
