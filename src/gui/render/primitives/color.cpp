namespace gfx {
	float Color::hue() const {
		const float red = r / 255.0f;
		const float green = g / 255.0f;
		const float blue = b / 255.0f;

		const float max_val = std::max({ red, green, blue });
		const float min_val = std::min({ red, green, blue });

		if (max_val == min_val) {
			return 0.0f;
		}

		float hue = 0.0f;
		const float delta = max_val - min_val;

		if (max_val == red) {
			hue = (green - blue) / delta + (green < blue ? 6.0f : 0.0f);
		}
		else if (max_val == green) {
			hue = (blue - red) / delta + 2.0f;
		}
		else {
			hue = (red - green) / delta + 4.0f;
		}

		hue /= 6.0f;
		return hue;
	}

	float Color::saturation() const {
		const float red = r / 255.0f;
		const float green = g / 255.0f;
		const float blue = b / 255.0f;

		const float max_val = std::max({ red, green, blue });
		const float min_val = std::min({ red, green, blue });

		if (max_val == 0.0f) {
			return 0.0f;
		}

		return (max_val - min_val) / max_val;
	}

	float Color::brightness() const {
		const float red = r / 255.0f;
		const float green = g / 255.0f;
		const float blue = b / 255.0f;

		return std::max({ red, green, blue });
	}

	std::tuple<float, float, float> Color::to_hsb() const {
		return { hue(), saturation(), brightness() };
	}

	Color Color::from_hex(uint32_t hex) {
		return { static_cast<uint8_t>((hex >> 24) & 0xFF),
			     static_cast<uint8_t>((hex >> 16) & 0xFF),
			     static_cast<uint8_t>((hex >> 8) & 0xFF),
			     static_cast<uint8_t>(hex & 0xFF) };
	}

	std::string Color::to_hex_string(bool include_alpha) const {
		std::stringstream ss;
		ss << "#";

		auto append_hex = [&ss](uint8_t value) {
			ss << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(value);
		};

		append_hex(r);
		append_hex(g);
		append_hex(b);

		if (include_alpha) {
			append_hex(a);
		}

		return ss.str();
	}

	std::optional<Color> Color::from_hex_string(const std::string& hex_str, bool allow_alpha) {
		std::string hex = hex_str;

		if (hex.empty())
			return std::nullopt;

		if (hex[0] == '#') {
			hex = hex.substr(1);
		}

		if (hex.length() != 6 && hex.length() != 8) {
			return std::nullopt;
		}

		uint32_t value = 0;
		try {
			value = std::stoul(hex, nullptr, 16);
		}
		catch (...) {
			return std::nullopt;
		}

		Color out;
		if (hex.length() == 6) {
			return Color(
				// r
				(value >> 16) & 0xFF,
				// g
				(value >> 8) & 0xFF,
				// b
				value & 0xFF,
				// a
				255
			);
		}
		else if (allow_alpha) {
			return Color(
				// r
				(value >> 24) & 0xFF,
				// g
				(value >> 16) & 0xFF,
				// b
				(value >> 8) & 0xFF,
				// a
				value & 0xFF
			);
		}

		return std::nullopt;
	}

	Color Color::from_hsb(float h, float s, float b, uint8_t alpha) {
		h = std::fmod(h, 1.0f) * 6.0f;
		s = std::clamp(s, 0.0f, 1.0f);
		b = std::clamp(b, 0.0f, 1.0f);

		const int i = static_cast<int>(h);
		const float f = h - i;
		const float p = b * (1.0f - s);
		const float q = b * (1.0f - s * f);
		const float t = b * (1.0f - s * (1.0f - f));

		float r = 0.0f;
		float g = 0.0f;
		float blu = 0.0f;

		switch (i) {
			case 0:
			case 6:
				r = b;
				g = t;
				blu = p;
				break;
			case 1:
				r = q;
				g = b;
				blu = p;
				break;
			case 2:
				r = p;
				g = b;
				blu = t;
				break;
			case 3:
				r = p;
				g = q;
				blu = b;
				break;
			case 4:
				r = t;
				g = p;
				blu = b;
				break;
			case 5:
				r = b;
				g = p;
				blu = q;
				break;
			default:
				break;
		}

		return { static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(blu * 255), alpha };
	}

	Color Color::lerp(const Color& from, const Color& to, float t) {
		t = std::clamp(t, 0.0f, 1.0f);

		return { static_cast<uint8_t>(from.r + ((to.r - from.r) * t)),
			     static_cast<uint8_t>(from.g + ((to.g - from.g) * t)),
			     static_cast<uint8_t>(from.b + ((to.b - from.b) * t)),
			     static_cast<uint8_t>(from.a + ((to.a - from.a) * t)) };
	}

	Color Color::lerp_hsb(const Color& from, const Color& to, float t) {
		t = std::clamp(t, 0.0f, 1.0f);

		auto [h1, s1, b1] = from.to_hsb();
		auto [h2, s2, b2] = to.to_hsb();

		// Special case for hue to ensure we take the shortest path around the color wheel
		float h_diff = h2 - h1;
		if (h_diff > 0.5f)
			h_diff -= 1.0f;
		if (h_diff < -0.5f)
			h_diff += 1.0f;

		float h = h1 + (h_diff * t);
		if (h < 0.0f)
			h += 1.0f;
		if (h > 1.0f)
			h -= 1.0f;

		float s = s1 + ((s2 - s1) * t);
		float b = b1 + ((b2 - b1) * t);
		auto a = static_cast<uint8_t>(from.a + ((to.a - from.a) * t));

		return from_hsb(h, s, b, a);
	}

}
