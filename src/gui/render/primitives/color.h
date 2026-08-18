
#pragma once

namespace gfx {
	class Color {
	public:
		uint8_t r = 255;
		uint8_t g = 255;
		uint8_t b = 255;
		uint8_t a = 255;

		// Constructors
		constexpr Color() = default;

		constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}

		constexpr Color(const Color& color, uint8_t alpha) : r(color.r), g(color.g), b(color.b), a(alpha) {}

		constexpr Color(uint32_t hex)
			: r((hex >> 0) & 0xFF), g((hex >> 8) & 0xFF), b((hex >> 16) & 0xFF), a((hex >> 24) & 0xFF) {}

		// Accessors
		uint8_t& operator[](int i) {
			return reinterpret_cast<uint8_t*>(this)[i];
		}

		uint8_t operator[](int i) const {
			return reinterpret_cast<const uint8_t*>(this)[i];
		}

		// Alpha operations
		[[nodiscard]] constexpr Color adjust_alpha(float factor) const {
			return { r, g, b, static_cast<uint8_t>(std::clamp(a * factor, 0.0f, 255.0f)) };
		}

		[[nodiscard]] constexpr Color with_alpha(uint8_t alpha) const {
			return { r, g, b, alpha };
		}

		// Color space conversions
		[[nodiscard]] float hue() const;
		[[nodiscard]] float saturation() const;
		[[nodiscard]] float brightness() const;
		[[nodiscard]] std::tuple<float, float, float> to_hsb() const;

		// Format conversions
		[[nodiscard]] constexpr uint32_t to_rgba() const {
			return (static_cast<uint32_t>(r) << 0) | (static_cast<uint32_t>(g) << 8) |
			       (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
		}

		[[nodiscard]] constexpr uint32_t to_argb() const {
			return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
			       (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 0);
		}

		[[nodiscard]] constexpr uint32_t to_abgr() const {
			return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
			       (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(r) << 0);
		}

		[[nodiscard]] constexpr uint32_t to_imgui() const {
			return to_abgr();
		}

		[[nodiscard]] std::string to_hex_string(bool include_alpha = false) const;

		// Operators
		constexpr bool operator==(const Color& other) const {
			return r == other.r && g == other.g && b == other.b && a == other.a;
		}

		constexpr bool operator!=(const Color& other) const {
			return !(*this == other);
		}

		constexpr Color& operator*=(float factor) {
			r = static_cast<uint8_t>(std::clamp(r * factor, 0.0f, 255.0f));
			g = static_cast<uint8_t>(std::clamp(g * factor, 0.0f, 255.0f));
			b = static_cast<uint8_t>(std::clamp(b * factor, 0.0f, 255.0f));
			a = static_cast<uint8_t>(std::clamp(a * factor, 0.0f, 255.0f));
			return *this;
		}

		constexpr Color operator*(float factor) const {
			Color result = *this;
			result *= factor;
			return result;
		}

		constexpr Color& operator/=(float divisor) {
			return *this *= (1.0f / divisor);
		}

		constexpr Color operator/(float divisor) const {
			Color result = *this;
			result /= divisor;
			return result;
		}

		// Static color creation methods
		static Color from_hex(uint32_t hex);
		static std::optional<Color> from_hex_string(const std::string& hex, bool allow_alpha);
		static Color from_hsb(float hue, float saturation, float brightness, uint8_t alpha = 255);

		// Interpolation
		static Color lerp(const Color& from, const Color& to, float t);
		static Color lerp_hsb(const Color& from, const Color& to, float t);

		// Common colors
		static constexpr Color black(uint8_t a = 255) {
			return { 0, 0, 0, a };
		}

		static constexpr Color white(uint8_t a = 255) {
			return { 255, 255, 255, a };
		}

		static constexpr Color red(uint8_t a = 255) {
			return { 255, 0, 0, a };
		}

		static constexpr Color green(uint8_t a = 255) {
			return { 0, 255, 0, a };
		}

		static constexpr Color blue(uint8_t a = 255) {
			return { 0, 0, 255, a };
		}

		static constexpr Color yellow(uint8_t a = 255) {
			return { 255, 255, 0, a };
		}

		static constexpr Color cyan(uint8_t a = 255) {
			return { 0, 255, 255, a };
		}

		static constexpr Color magenta(uint8_t a = 255) {
			return { 255, 0, 255, a };
		}

		static constexpr Color transparent() {
			return { 0, 0, 0, 0 };
		}

		static constexpr Color gray(uint8_t value = 128, uint8_t a = 255) {
			return { value, value, value, a };
		}
	};
}
