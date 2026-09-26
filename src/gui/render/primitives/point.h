
#pragma once

#include <cmath>

namespace gfx {
	class Size;
	class Rect;

	class Point {
	public:
		int x = 0;
		int y = 0;

		constexpr Point() = default;

		constexpr Point(int x, int y) : x(x), y(y) {}

		// Core operations
		[[nodiscard]] constexpr Point offset(int dx, int dy) const {
			return { x + dx, y + dy };
		}

		[[nodiscard]] constexpr Point offset(int d) const {
			return offset(d, d);
		}

		[[nodiscard]] constexpr Point offset_x(int dx) const {
			return { x + dx, y };
		}

		[[nodiscard]] constexpr Point offset_y(int dy) const {
			return { x, y + dy };
		}

		[[nodiscard]] constexpr bool in_rect(const Rect& rect) const;

		[[nodiscard]] constexpr bool is_zero() const {
			return x == 0 && y == 0;
		}

		[[nodiscard]] constexpr Size to_size() const;

		// Distance calculations
		[[nodiscard]] float distance(const Point& other) const {
			return std::sqrt(static_cast<float>(((x - other.x) * (x - other.x)) + ((y - other.y) * (y - other.y))));
		}

		[[nodiscard]] float length() const {
			return std::sqrt(static_cast<float>((x * x) + (y * y)));
		}

		// Operators
		constexpr bool operator==(const Point& other) const {
			return x == other.x && y == other.y;
		}

		constexpr bool operator!=(const Point& other) const {
			return !(*this == other);
		}

		constexpr Point& operator+=(const Point& other) {
			x += other.x;
			y += other.y;
			return *this;
		}

		constexpr Point operator+(const Point& other) const {
			Point result = *this;
			result += other;
			return result;
		}

		constexpr Point& operator-=(const Point& other) {
			x -= other.x;
			y -= other.y;
			return *this;
		}

		constexpr Point operator-(const Point& other) const {
			Point result = *this;
			result -= other;
			return result;
		}

		constexpr Point operator-() const {
			return { -x, -y };
		}

		constexpr Point& operator*=(float scale) {
			x = static_cast<int>(x * scale);
			y = static_cast<int>(y * scale);
			return *this;
		}

		constexpr Point operator*(float scale) const {
			Point result = *this;
			result *= scale;
			return result;
		}

		constexpr Point& operator/=(float divisor) {
			x = static_cast<int>(x / divisor);
			y = static_cast<int>(y / divisor);
			return *this;
		}

		constexpr Point operator/(float divisor) const {
			Point result = *this;
			result /= divisor;
			return result;
		}

		constexpr Point& operator+=(const Size& size);
		constexpr Point operator+(const Size& size) const;
		constexpr Point& operator-=(const Size& size);
		constexpr Point operator-(const Size& size) const;
	};
}
