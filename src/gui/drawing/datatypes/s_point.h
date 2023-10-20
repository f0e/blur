#pragma once

struct s_size;
struct s_rect;

struct s_point {
public:
	int x, y;

public:
	constexpr s_point()
			: x{ 0 }, y{ 0 } {}

	constexpr s_point(int _x, int _y)
			: x(_x),
				y(_y) {}

public:
	constexpr s_point offset(int offset) const {
		return {
			x + offset,
			y + offset
		};
	}

	constexpr s_point offset(int x_offset, int y_offset) const {
		return {
			x + x_offset,
			y + y_offset
		};
	}

	constexpr s_point offset_x(int x_offset) const {
		return {
			x + x_offset,
			y
		};
	}

	constexpr s_point offset_y(int y_offset) const {
		return {
			x,
			y + y_offset
		};
	}

	bool in_rect(const s_rect& r) const;
	s_size to_size() const;

	constexpr inline bool is_zero() const {
		return !x && !y;
	}

public:
	constexpr bool operator==(const s_point& v) const {
		return x == v.x && y == v.y;
	}

	constexpr s_point& operator+=(const s_point& v) {
		x += v.x;
		y += v.y;
		return *this;
	}

	constexpr s_point operator+(const s_point& v) const {
		auto tmp = *this;
		tmp += v;
		return tmp;
	}

	constexpr s_point operator*(const s_point& v) {
		auto tmp = *this;
		tmp *= v;
		return tmp;
	}

	constexpr s_point operator/(const s_point& v) {
		auto tmp = *this;
		tmp /= v;
		return tmp;
	}

	constexpr s_point& operator-=(const s_point& v) {
		x -= v.x;
		y -= v.y;
		return *this;
	}

	constexpr s_point& operator/=(const s_point& v) {
		x /= v.x;
		y /= v.y;
		return *this;
	}

	constexpr s_point operator-(const s_point& v) const {
		auto tmp = *this;
		tmp -= v;
		return tmp;
	}

	s_point& operator+=(const s_size& v);
	s_point operator+(const s_size& v) const {
		auto tmp = *this;
		tmp += v;
		return tmp;
	}

	s_point& operator-=(const s_size& v);
	s_point operator-(const s_size& v) const {
		auto tmp = *this;
		tmp -= v;
		return tmp;
	}

	constexpr s_point& operator*=(const s_point& v) {
		x *= v.x;
		y *= v.y;
		return *this;
	}

	s_point& operator*=(float amt) {
		x = x * amt;
		y = y * amt;
		return *this;
	}

	s_point operator*(float amt) const {
		auto tmp = *this;
		tmp *= amt;
		return tmp;
	}

	s_point& operator/=(float amt) {
		x = x / amt;
		y = y / amt;
		return *this;
	}

	s_point operator/(float amt) const {
		auto tmp = *this;
		tmp /= amt;
		return tmp;
	}

	constexpr bool operator==(const s_point& v) {
		return x == v.x && y == v.y;
	}
};

#define IM_VEC2_CLASS_EXTRA                                     \
        ImVec2(const s_point& f) : x(f.x), y(f.y) {}  \
        operator s_point() const { return s_point((int)x, (int)y); }