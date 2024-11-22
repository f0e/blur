#pragma once

const inline double ease_out_quart(double x) {
	return 1.f - pow(1.f - x, 4.f);
}

const inline double ease_out_expo(double x) {
	return (x == 1) ? 1 : 1 - std::pow(2, -10 * x);
}
