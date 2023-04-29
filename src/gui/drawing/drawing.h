#pragma once

#include "datatypes/datatypes.h"
#include "font/font.h"

struct ImGuiContext;
struct ImGuiIO;
struct ImDrawList;

class i_texture;
struct GLFWwindow;

enum e_font_flags : unsigned int {
	FONT_NONE = 0,
	FONT_CENTERED_X = (1 << 0),
	FONT_CENTERED_Y = (1 << 1),
	FONT_DROPSHADOW = (1 << 2),
	FONT_OUTLINE = (1 << 3),
	FONT_RIGHT_ALIGN = (1 << 4),
	FONT_LEFT_ALIGN = (1 << 5),
	FONT_BOTTOM_ALIGN = (1 << 6),
};

enum e_rounding_flags : unsigned int { // c+p from imgui
	ROUNDING_TOP_LEFT = 1 << 4,          // add_rect(), add_rect_filled(), path_rect(): enable rounding top-left corner only (when rounding > 0.0f, we default to all corners). was 0x01.
	ROUNDING_TOP_RIGHT = 1 << 5,         // add_rect(), add_rect_filled(), path_rect(): enable rounding top-right corner only (when rounding > 0.0f, we default to all corners). was 0x02.
	ROUNDING_BOTTOM_LEFT = 1 << 6,       // add_rect(), add_rect_filled(), path_rect(): enable rounding bottom-left corner only (when rounding > 0.0f, we default to all corners). was 0x04.
	ROUNDING_BOTTOM_RIGHT = 1 << 7,      // add_rect(), add_rect_filled(), path_rect(): enable rounding bottom-right corner only (when rounding > 0.0f, we default to all corners). wax 0x08.
	ROUNDING_NONE = 1 << 8,              // add_rect(), add_rect_filled(), path_rect(): disable rounding on all corners (when rounding > 0.0f). this is not zero, not an implicit flag!

	ROUNDING_TOP = ROUNDING_TOP_LEFT | ROUNDING_TOP_RIGHT,
	ROUNDING_BOTTOM = ROUNDING_BOTTOM_LEFT | ROUNDING_BOTTOM_RIGHT,
	ROUNDING_LEFT = ROUNDING_BOTTOM_LEFT | ROUNDING_TOP_LEFT,
	ROUNDING_RIGHT = ROUNDING_BOTTOM_RIGHT | ROUNDING_TOP_RIGHT,
};

enum e_gradient_type {
	GRADIENT_VERTICAL,
	GRADIENT_HORIZONTAL
};

namespace fonts {
	inline drawing::font mono;
}

namespace drawing {
	inline bool initialized = false;
	inline bool unload = false;
	inline int w = 0, h = 0;
	inline s_point screen_center;
	inline s_size screen_res;

	inline std::stack<float> alphas{};
	inline std::stack<s_point> transforms{};

	inline float frametime;

	struct s_imgui {
		GLFWwindow* window;

		ImGuiContext* ctx;
		ImGuiIO* io;
		ImDrawList* drawlist;

		bool init(GLFWwindow* glfw_window);

		void begin();

		void end();
	} inline imgui;

	bool init(GLFWwindow* glfw_window);

	s_point adjust_position(const s_point& pos);
	s_rect adjust_position(const s_rect& rect);

	void line(const s_point& pos1, const s_point& pos2, const s_colour& col, bool anti_aliased = false, float thickness = 1.f);
	void line(int x1, int y1, int x2, int y2, const s_colour& col, bool anti_aliased = false, float thickness = 1.f);

	void rect(const s_rect& bounds, const s_colour& col, bool filled = false, float rounding = 0.f, int rounding_flags = 0);
	void rect(const s_point& top_left, const s_point& bottom_right, const s_colour& col, bool filled = false, float rounding = 0.f, int rounding_flags = 0);
	void rect(int x1, int y1, int x2, int y2, const s_colour& col, bool filled = false, float rounding = 0.f, int rounding_flags = 0);

	void rect_gradient_multi(const s_point& top_left, const s_point& bottom_right, const s_colour& col_top_left, const s_colour& col_top_right, const s_colour& col_bottom_left, const s_colour& col_bottom_right, bool filled = false);
	void rect_gradient_multi(int x1, int y1, int x2, int y2, const s_colour& col_top_left, const s_colour& col_top_right, const s_colour& col_bottom_left, const s_colour& col_bottom_right, bool filled = false);

	void quadrilateral(const s_point& bottom_left, const s_point& bottom_right, const s_point& top_left, const s_point& top_right, const s_colour& col, bool filled = false, float thickness = 1.f);

	void triangle(const s_point& p1, const s_point& p2, const s_point& p3, const s_colour& col, bool filled = false, float thickness = 1.f, bool anti_aliased = false);

	void circle(const s_point& pos, float radius, const s_colour& s_colour, bool filled = false, float thickness = 1.f, int parts = 50, float degrees = 360.f, float start_degree = 0.f, bool anti_aliased = true);

	void string(s_point pos, int flags, const s_colour& colour, const font& font, const std::string& text);
	void string(s_point pos, int flags, const s_colour& colour, const font& font, const std::wstring& text);

	void push_clip_rect(const s_rect& rect, bool intersect_clip_rect = false);
	void push_clip_rect(int x1, int y1, int x2, int y2, bool intersect_clip_rect = false);
	void push_fullscreen_clip_rect();
	s_rect pop_clip_rect();
	s_rect get_clip_rect();

	void push_alpha(float alpha);
	void push_relative_alpha(float alpha);
	void pop_alpha();
	std::optional<float> get_alpha();

	void push_transform(s_point transform);
	void push_relative_transform(s_point transform);
	void pop_transform();
	std::optional<s_point> get_transform();

	bool clip_string(std::string& text, const font& font, int max_width, int min_chars = 0);

	void animate(float& val, float goal, float speed);
	void round(float& val, float goal, float round_diff);
}