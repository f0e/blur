#pragma once

namespace gfx {
	class Point;
	class Size;

	class Rect {
	public:
		int x = 0;
		int y = 0;
		int w = 0;
		int h = 0;

		constexpr Rect() = default;

		constexpr Rect(int x, int y, int w, int h) : x(x), y(y), w(w), h(h) {}

		constexpr Rect(const Point& position, const Size& size);
		constexpr Rect(const Point& p1, const Point& p2);

		// Position and size accessors
		[[nodiscard]] constexpr Point position() const;

		constexpr void set_position(const Point& pos);
		constexpr void add_position(const Point& offset);

		[[nodiscard]] constexpr Size size() const;

		constexpr void set_size(const Size& sz);

		constexpr void set_size(int size) {
			w = size;
			h = size;
		}

		constexpr void add_size(const Size& sz);

		// Boundary and center calculations
		[[nodiscard]] constexpr int x2() const {
			return x + w;
		}

		[[nodiscard]] constexpr int y2() const {
			return y + h;
		}

		[[nodiscard]] constexpr Point top_left() const;
		[[nodiscard]] constexpr Point top_right() const;
		[[nodiscard]] constexpr Point bottom_left() const;
		[[nodiscard]] constexpr Point bottom_right() const;
		[[nodiscard]] constexpr Point top_center() const;
		[[nodiscard]] constexpr Point bottom_center() const;
		[[nodiscard]] constexpr Point left_center() const;
		[[nodiscard]] constexpr Point right_center() const;
		[[nodiscard]] constexpr Point center() const;

		[[nodiscard]] constexpr Point origin() const {
			return top_left();
		}

		[[nodiscard]] constexpr Point max() const {
			return bottom_right();
		}

		// Utility methods
		[[nodiscard]] constexpr bool is_zero() const {
			return x == 0 && y == 0 && w == 0 && h == 0;
		}

		[[nodiscard]] constexpr bool is_empty() const {
			return w <= 0 || h <= 0;
		}

		[[nodiscard]] constexpr bool contains(const Point& pt) const;

		[[nodiscard]] constexpr bool contains(const Rect& other) const {
			return other.x >= x && other.y >= y && other.x + other.w <= x + w && other.y + other.h <= y + h;
		}

		[[nodiscard]] constexpr bool intersects(const Rect& other) const {
			return x < other.x + other.w && x + w > other.x && y < other.y + other.h && y + h > other.y;
		}

		// Transformation
		constexpr void vertical_cut(int value, bool centered = false) {
			if (centered) {
				y += value;
				h -= value * 2;
			}
			else {
				y += value;
				h -= value;
			}
		}

		constexpr void horizontal_cut(int value, bool centered = false) {
			if (centered) {
				x += value;
				w -= value * 2;
			}
			else {
				x += value;
				w -= value;
			}
		}

		[[nodiscard]] constexpr Rect expand(int amount, bool centered = true) const {
			if (centered) {
				return { x - amount, y - amount, w + (amount * 2), h + (amount * 2) };
			}
			return { x, y, w + amount, h + amount };
		}

		[[nodiscard]] constexpr Rect expand(const Size& amount, bool centered = true) const;

		[[nodiscard]] constexpr Rect shrink(int amount, bool centered = true) const {
			return expand(-amount, centered);
		}

		[[nodiscard]] constexpr Rect shrink(const Size& amount, bool centered) const;

		[[nodiscard]] constexpr Rect offset(int amount) const {
			return { x + amount, y + amount, w, h };
		}

		[[nodiscard]] constexpr Rect intersect(const Rect& other) const {
			int nx = std::max(x, other.x);
			int ny = std::max(y, other.y);
			int nw = std::min(x + w, other.x + other.w) - nx;
			int nh = std::min(y + h, other.y + other.h) - ny;

			if (nw <= 0 || nh <= 0)
				return {};
			return { nx, ny, nw, nh };
		}

		[[nodiscard]] constexpr Rect merge(const Rect& other) const {
			if (is_empty())
				return other;
			if (other.is_empty())
				return *this;

			int nx = std::min(x, other.x);
			int ny = std::min(y, other.y);
			int nx2 = std::max(x + w, other.x + other.w);
			int ny2 = std::max(y + h, other.y + other.h);

			return { nx, ny, nx2 - nx, ny2 - ny };
		}

		// methods that need external state or implementation
		[[nodiscard]] bool hovered() const;
		[[nodiscard]] float mouse_percent_x(bool uncapped = false) const;
		[[nodiscard]] float mouse_percent_y(bool uncapped = false) const;
		[[nodiscard]] bool on_screen() const;
		void clamp_to(const Rect& boundary);

		constexpr Rect operator+(const Point& offset) const;
		constexpr Rect operator-(const Point& offset) const;
		constexpr Rect& operator+=(const Point& offset);
		constexpr Rect& operator-=(const Point& offset);
	};
}
