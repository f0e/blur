#pragma once

#include <cmath>

namespace video::frame_snap {
	[[nodiscard]] inline double snap_time(double time, double fps) {
		if (fps <= 0.0)
			return time;

		return std::round(time * fps) / fps;
	}

	[[nodiscard]] inline float snap_percent(float percent, double duration, double fps) {
		if (duration <= 0.0)
			return percent;

		return static_cast<float>(snap_time(percent * duration, fps) / duration);
	}
} // namespace video::frame_snap
