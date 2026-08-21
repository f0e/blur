#pragma once

#include <span>

#include "font/font.h"

enum EFontFlags : unsigned int {
	FONT_NONE = 0,
	FONT_CENTERED_X = (1 << 0),
	FONT_CENTERED_Y = (1 << 1),
	FONT_DROPSHADOW = (1 << 2),
	FONT_OUTLINE = (1 << 3),
	FONT_RIGHT_ALIGN = (1 << 4),
	FONT_BOTTOM_ALIGN = (1 << 5),
};

enum ERoundingFlags : unsigned int { // c+p from imgui
	ROUNDING_TOP_LEFT = 1 << 4,      // add_rect(), add_rect_filled(), path_rect(): enable rounding top-left corner only
	                                 // (when rounding > 0.0f, we default to all corners). was 0x01.
	ROUNDING_TOP_RIGHT = 1 << 5,    // add_rect(), add_rect_filled(), path_rect(): enable rounding top-right corner only
	                                // (when rounding > 0.0f, we default to all corners). was 0x02.
	ROUNDING_BOTTOM_LEFT = 1 << 6,  // add_rect(), add_rect_filled(), path_rect(): enable rounding bottom-left corner
	                                // only (when rounding > 0.0f, we default to all corners). was 0x04.
	ROUNDING_BOTTOM_RIGHT = 1 << 7, // add_rect(), add_rect_filled(), path_rect(): enable rounding bottom-right corner
	                                // only (when rounding > 0.0f, we default to all corners). wax 0x08.
	ROUNDING_NONE = 1 << 8,         // add_rect(), add_rect_filled(), path_rect(): disable rounding on all corners (when
	                                // rounding > 0.0f). this is not zero, not an implicit flag!

	ROUNDING_TOP = ROUNDING_TOP_LEFT | ROUNDING_TOP_RIGHT,
	ROUNDING_BOTTOM = ROUNDING_BOTTOM_LEFT | ROUNDING_BOTTOM_RIGHT,
	ROUNDING_LEFT = ROUNDING_BOTTOM_LEFT | ROUNDING_TOP_LEFT,
	ROUNDING_RIGHT = ROUNDING_BOTTOM_RIGHT | ROUNDING_TOP_RIGHT,

	ROUNDING_ALL = ROUNDING_TOP | ROUNDING_BOTTOM | ROUNDING_LEFT | ROUNDING_RIGHT
};

namespace fonts {
	namespace size {
		inline constexpr float BODY = 13.f;
		inline constexpr float SMALL = 11.f;
		inline constexpr float HEADER = 32.f;
		inline constexpr float SMALL_HEADER = 20.f;
		inline constexpr float ICON = 14.f;
	}

	// these are initialised at their most common size - call them with another one to get the same
	// typeface resized, e.g. fonts::dejavu(fonts::size::SMALL)
	inline render::Font dejavu;   // size::BODY
	inline render::Font garamond; // size::HEADER
	inline render::Font icons;    // size::ICON
}

namespace render {
	inline bool initialised = false;
	inline float frametime;
	inline gfx::Size window_size;
	inline float ui_scale = 1.f;
	inline float dpi_scale_override = 0.f;
	inline std::vector<std::function<void()>> late_draw_calls;

	// Texture wrapper class for OpenGL textures
	class Texture {
	public:
		Texture() : m_id(0), m_width(0), m_height(0) {}

		~Texture();

		bool load_from_file(const std::string& path);
		bool load_from_surface(SDL_Surface* surface);
		void destroy();

		void bind() const;
		static void unbind();

		[[nodiscard]] ImTextureID get_id() const {
			return (ImTextureID)(intptr_t)m_id;
		}

		[[nodiscard]] int width() const {
			return m_width;
		}

		[[nodiscard]] int height() const {
			return m_height;
		}

		[[nodiscard]] bool is_valid() const {
			return m_id != 0;
		}

	private:
		unsigned int m_id;
		int m_width;
		int m_height;
	};

	struct ImGuiWrap {
		ImGuiContext* ctx;
		ImGuiIO* io;
		ImDrawList* drawlist;

		bool init(SDL_Window* window, const SDL_GLContext& context);

		void begin(SDL_Window* window);
		void end(SDL_Window* window);
	} inline imgui;

	float get_content_scale(SDL_Window* window);

	void update_window_size(SDL_Window* window);
	bool init(SDL_Window* window, const SDL_GLContext& context);
	void destroy();

	// TODO: antialiased doesnt do anything anywhere

	void line(
		const gfx::Point& pos1,
		const gfx::Point& pos2,
		const gfx::Color& col,
		bool anti_aliased = false,
		float thickness = 1.f
	);

	void rect_filled(const gfx::Rect& rect, const gfx::Color& col);
	void rect_stroke(const gfx::Rect& rect, const gfx::Color& col, float thickness = 1.f);

	void rounded_rect_filled(
		const gfx::Rect& rect, const gfx::Color& col, float rounding, unsigned int rounding_flags = ROUNDING_ALL
	);
	void rounded_rect_stroke(
		const gfx::Rect& rect,
		const gfx::Color& col,
		float rounding,
		unsigned int rounding_flags = ROUNDING_ALL,
		float thickness = 1.f
	);

	enum class GradientDirection : uint8_t {
		GRADIENT_LEFT,
		GRADIENT_RIGHT,
		GRADIENT_UP,
		GRADIENT_DOWN,
		GRADIENT_DOWN_RIGHT,
		GRADIENT_DOWN_LEFT,
		GRADIENT_UP_RIGHT,
		GRADIENT_UP_LEFT,
	};

	void rect_filled_gradient(
		const gfx::Rect& rect,
		const std::vector<gfx::Point>& gradient_direction,
		const std::vector<gfx::Color>& colors,
		const std::vector<float>& positions = { 0.f, 1.f }
	);

	void rect_filled_gradient(
		const gfx::Rect& rect,
		GradientDirection direction,
		const std::vector<gfx::Color>& colors,
		const std::vector<float>& positions = { 0.f, 1.f }
	);

	void rect_gradient_multi_filled(
		const gfx::Rect& rect,
		const gfx::Color& col_top_left,
		const gfx::Color& col_top_right,
		const gfx::Color& col_bottom_left,
		const gfx::Color& col_bottom_right
	);

	void quadrilateral_filled(
		const gfx::Point& bottom_left,
		const gfx::Point& bottom_right,
		const gfx::Point& top_left,
		const gfx::Point& top_right,
		const gfx::Color& col
	);
	void quadrilateral_stroke(
		const gfx::Point& bottom_left,
		const gfx::Point& bottom_right,
		const gfx::Point& top_left,
		const gfx::Point& top_right,
		const gfx::Color& col,
		float thickness = 1.f
	);

	void triangle_filled(const gfx::Point& p1, const gfx::Point& p2, const gfx::Point& p3, const gfx::Color& col);

	void triangle_stroke(
		const gfx::Point& p1,
		const gfx::Point& p2,
		const gfx::Point& p3,
		const gfx::Color& col,
		float thickness = 1.f,
		bool anti_aliased = false
	);

	void circle_filled(
		const gfx::Point& pos,
		float radius,
		const gfx::Color& colour,
		float thickness = 1.f,
		int parts = 50,
		float degrees = 360.f,
		float start_degree = 0.f,
		bool anti_aliased = true
	);

	void circle_stroke(
		const gfx::Point& pos,
		float radius,
		const gfx::Color& colour,
		float thickness = 1.f,
		int parts = 50,
		float degrees = 360.f,
		float start_degree = 0.f,
		bool anti_aliased = true
	);

	void text(
		gfx::Point pos,
		const gfx::Color& colour,
		const std::string& text,
		const Font& font,
		unsigned int flags = FONT_NONE,
		float rotation_deg = 0.f,
		int rotation_pivot_y = 0
	);

	void image(const gfx::Rect& rect, const Texture& texture, const gfx::Color& tint_color = gfx::Color::white());

	void image_rounded(
		const gfx::Rect& rect,
		const Texture& texture,
		float rounding,
		unsigned int rounding_flags = ROUNDING_ALL,
		const gfx::Color& tint_color = gfx::Color::white()
	);

	void rounded_image_with_borders(
		const gfx::Rect& rect,
		const Texture& texture,
		float rounding,
		const gfx::Color& border_color,
		const gfx::Color& inner_border_color,
		float border_thickness = 1.0f,
		unsigned int rounding_flags = ROUNDING_ALL,
		const gfx::Color& tint_color = gfx::Color::white()
	);

	void borders(const gfx::Rect& rect, const gfx::Color& border_color, const gfx::Color& inner_border_color);

	void spinner(
		const gfx::Point& pos,
		float radius,
		const gfx::Color& background_color,
		const gfx::Color& highlight_color,
		float thickness = 2.f,
		float alpha = 1.f,
		float trail_degrees = 180.f
	);

	void waveform(
		const gfx::Rect& rect,
		const gfx::Rect& active_rect,
		const gfx::Color& color,
		const std::vector<int16_t>& samples,
		int16_t max_sample,
		float zoom_start = 0.0f,
		float zoom_end = 1.0f
	);

	enum class RectSide {
		LEFT,
		RIGHT
	};

	void rect_side(const gfx::Rect& rect, const gfx::Color& color, RectSide side, int thickness = 1);

	void push_clip_rect(const gfx::Rect& rect, bool intersect_clip_rect = false);
	void push_clip_rect(int x1, int y1, int x2, int y2, bool intersect_clip_rect = false);
	void push_fullscreen_clip_rect();
	gfx::Rect pop_clip_rect();
	gfx::Rect get_clip_rect();

	// Captures and transforms a group of already-submitted primitives as one unit.
	[[nodiscard]] size_t draw_vertex_count();
	void transform_draw_vertices(size_t first_vertex, const gfx::Rect& from, const gfx::Rect& to, float opacity = 1.f);

	bool clip_string(std::string& text, const Font& font, int max_width, int min_chars = 0);

	// Wraps without touching the text: every space, tab and blank line survives, and lines break at the last space
	// that fits (splitting a word only when it's wider than the field on its own). wrap_text is for prose - it
	// re-flows on whitespace, which mangles anything where the spacing carries meaning, like a log or a traceback.
	std::vector<std::string> wrap_text_verbatim(const std::string& text, int max_width, const Font& font);

	std::vector<std::string> wrap_text(
		const std::string& text, const gfx::Size& dimensions, const Font& font, int line_height = 0
	);

	SDL_Surface* jpeg_bytes_to_surface(const void* data, size_t size);
	SDL_Surface* png_bytes_to_surface(const void* data, size_t size);

	// decode + upload in one go, null on empty input or failure. must be called on the render thread
	std::shared_ptr<Texture> texture_from_jpeg(std::span<const uint8_t> jpeg);
}
