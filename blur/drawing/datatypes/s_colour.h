#pragma once

class s_colour {
public:
	uint8_t r, g, b, a;

	constexpr s_colour(uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint8_t a = 255)
			: r(r), g(g), b(b), a(a){};

	constexpr s_colour(const s_colour& col, uint8_t alpha)
			: r(col.r), g(col.g), b(col.b), a(alpha){};

	constexpr s_colour(uint32_t hex)
			: r(hex & 0xFF), g((hex >> 8) & 0xFF), b((hex >> 16) & 0xFF), a((hex >> 24) & 0xFF) {};

	constexpr static s_colour from_hex(uint32_t hex) {
		return { (hex >> 24) & 0xFF, (hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF };
	}

	uint8_t& operator[](int i) {
		return ((uint8_t*)this)[i];
	}

	uint8_t operator[](int i) const {
		return ((uint8_t*)this)[i];
	}

	uint8_t& operator[](uint8_t i) {
		return ((uint8_t*)this)[i];
	}

	uint8_t operator[](uint8_t i) const {
		return ((uint8_t*)this)[i];
	}

	s_colour adjust_alpha(const float& alpha) const {
		return s_colour(r, g, b, a * std::clamp(alpha, 0.f, 1.f));
	}

	s_colour override_alpha(uint8_t alpha) const {
		return s_colour(r, g, b, alpha);
	}

	constexpr s_colour& operator*=(const float& coeff) {
		r *= coeff;
		g *= coeff;
		b *= coeff;
		a *= coeff;
		return *this;
	}

	constexpr s_colour& operator/=(const float& div) {
		const float divided = 1.f / div;
		*this *= divided;
		return *this;
	}

	bool operator==(const s_colour& c) const {
		return r == c.r &&
		       g == c.g &&
		       b == c.b &&
		       a == c.a;
	}

	bool operator!=(const s_colour& c) const {
		return !(operator==(c));
	}

	s_colour operator()(const uint8_t new_a) const {
		return s_colour(r, g, b, new_a);
	}

	uint32_t to_imgui() const {
		return (((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r);
	}

	uint32_t convert() const;

	static constexpr s_colour black(uint8_t a = 255) { return { 0, 0, 0, a }; }
	static constexpr s_colour gray(uint8_t a = 255) { return { 190, 190, 190, a }; }
	static constexpr s_colour white(uint8_t a = 255) { return { 255, 255, 255, a }; }
	static constexpr s_colour green(uint8_t a = 255) { return { 0, 255, 0, a }; }
	static constexpr s_colour yellow(uint8_t a = 255) { return { 255, 255, 0, a }; }
	static constexpr s_colour orange(uint8_t a = 255) { return { 255, 128, 0, a }; }
	static constexpr s_colour red(uint8_t a = 255) { return { 255, 0, 0, a }; }
	static constexpr s_colour magenta(uint8_t a = 255) { return { 255, 0, 255, a }; }
	static constexpr s_colour blue(uint8_t a = 255) { return { 0, 0, 255, a }; }
	static constexpr s_colour cyan(uint8_t a = 255) { return { 0, 255, 255, a }; }
};
