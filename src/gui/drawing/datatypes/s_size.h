#pragma once

struct s_rect;

struct s_size {
public:
	int w = 0;
	int h = 0;

public:
	constexpr s_size() = default;

	constexpr s_size(int w, int h)
			: w(w), h(h) {}

public:
	constexpr s_size expand(int amt) const {
		s_size tmp = *this;
		tmp.w += amt;
		tmp.h += amt;
		return tmp;
	}

	constexpr s_size shrink(int amt) const {
		return expand(-amt);
	}

	inline bool is_zero() const {
		return !w && !h;
	}

	constexpr s_point to_point() const {
		return { w, h };
	}

	constexpr float aspect_ratio() const {
		return w / float(h);
	}

public:
	s_size& operator+=(const s_size& v) {
		w += v.w;
		h += v.h;
		return *this;
	}

	s_size operator+(const s_size& v) const {
		auto tmp = *this;
		tmp += v;
		return tmp;
	}

	s_size& operator-=(const s_size& v) {
		w -= v.w;
		h -= v.h;
		return *this;
	}

	s_size operator-(const s_size& v) const {
		auto tmp = *this;
		tmp -= v;
		return tmp;
	}

	s_size operator-() const {
		return { -w, -h };
	}

	s_size operator/(const s_size& v) const {
		auto tmp = *this;
		tmp /= v;
		return tmp;
	}

	s_size& operator*=(const float& amt) {
		w = w * amt;
		h = h * amt;
		return *this;
	}

	s_size operator*(const float& amt) const {
		auto tmp = *this;
		tmp *= amt;
		return tmp;
	}

	s_size& operator/=(const s_size& v) {
		w /= v.w;
		h /= v.h;
		return *this;
	}

	s_size& operator/=(const float& amt) {
		w = w / amt;
		h = h / amt;
		return *this;
	}

	s_size operator/(const float& amt) const {
		auto tmp = *this;
		tmp /= amt;
		return tmp;
	}

	bool operator==(const s_size& v) {
		return w == v.w && h == v.h;
	}
};

template<>
struct std::hash<s_size> {
	std::size_t operator()(s_size const& s) const noexcept {
		std::hash<int> hasher;
		std::size_t h1 = hasher(s.w);
		std::size_t h2 = hasher(s.h);
		return h1 ^ (h2 << 1);
	}
};
