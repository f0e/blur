#include "drawing.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <glfw/glfw3.h>

constexpr double M_PI = 3.14159265358979323846;

template<typename T>
inline constexpr T rad_to_deg(T radian) {
	return radian * (180.f / M_PI);
}

template<typename T>
inline constexpr T deg_to_rad(T degree) {
	return static_cast<T>(degree * (M_PI / 180.f));
}

const char* glsl_version = "#version 130";

bool drawing::s_imgui::init(GLFWwindow* glfw_window) {
	window = glfw_window;

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ctx = ImGui::CreateContext();
	io = &ImGui::GetIO();
	io->IniFilename = NULL;
	io->LogFilename = NULL;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	return true;
}

bool drawing::init(GLFWwindow* glfw_window) {
	if (!imgui.init(glfw_window))
		return false;

	ImFontConfig big_cfg;

	const auto wide_glyph_ranges = imgui.io->Fonts->GetGlyphRangesWide();

	// init fonts
	fonts::mono.init("C:\\Windows\\Fonts\\consola.ttf", 14.f, nullptr, wide_glyph_ranges);
	
	return true;
}

void drawing::s_imgui::begin() {
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	drawlist = ImGui::GetForegroundDrawList();

	// calculate frametime
	static std::chrono::high_resolution_clock timer;

	auto now = timer.now();
	static auto last = now;

	frametime = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(now - last).count() / 1000.f;

	last = now;
}

void drawing::s_imgui::end() {
	ImVec4 clear_colour = ImVec4(0.f, 0.f, 0.f, 1.f);

	ImGui::Render();
	int display_w, display_h;
	glfwGetFramebufferSize(window, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	glClearColor(clear_colour.x * clear_colour.w, clear_colour.y * clear_colour.w, clear_colour.z * clear_colour.w, clear_colour.w);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

s_point drawing::adjust_position(const s_point& pos) {
	if (transforms.empty())
		return pos;

	const auto& transform = transforms.top();
	return pos + transform;
}

s_rect drawing::adjust_position(const s_rect& rect) {
	if (transforms.empty())
		return rect;

	const auto& transform = transforms.top();
	return s_rect(rect.pos() + transform, rect.size());
}

void drawing::line(const s_point& pos1, const s_point& pos2, const s_colour& col, bool anti_aliased, float thickness) {
	imgui.drawlist->AddLine(adjust_position(pos1), adjust_position(pos2), col.convert(), thickness);
}

void drawing::line(int x1, int y1, int x2, int y2, const s_colour& col, bool anti_aliased, float thickness) {
	line(s_point(x1, y1), s_point(x2, y2), col, anti_aliased, thickness);
}

void drawing::rect(const s_rect& bounds, const s_colour& col, bool filled, float rounding, int rounding_flags) {
	if (filled)
		imgui.drawlist->AddRectFilled(adjust_position(bounds.pos()), adjust_position(bounds.max()), col.convert(), rounding, rounding_flags);
	else
		imgui.drawlist->AddRect(adjust_position(bounds.pos()), adjust_position(bounds.max()), col.convert(), rounding, rounding_flags);
}

void drawing::rect(const s_point& top_left, const s_point& bottom_right, const s_colour& col, bool filled, float rounding, int rounding_flags) {
	rect(s_rect(top_left, bottom_right), col, filled, rounding, rounding_flags);
}

void drawing::rect(int x1, int y1, int x2, int y2, const s_colour& col, bool filled, float rounding, int rounding_flags) {
	rect(s_point(x1, y1), s_point(x2, y2), col, filled, rounding, rounding_flags);
}

void drawing::rect_gradient_multi(const s_point& top_left, const s_point& bottom_right, const s_colour& col_top_left, const s_colour& col_top_right, const s_colour& col_bottom_left, const s_colour& col_bottom_right, bool filled) {
	rect(top_left, bottom_right, col_top_left, filled);
	if (filled)
		imgui.drawlist->AddRectFilledMultiColor(adjust_position(top_left), adjust_position(bottom_right), col_top_left.convert(), col_top_right.convert(), col_bottom_right.convert(), col_bottom_left.convert());
	// else TODO
	// 	imgui.drawlist->rectangle(adjust_position(top_left), adjust_position(bottom_right), 1.f, convert_col(col_top_left), convert_col(col_top_right), convert_col(col_bottom_left), convert_col(col_bottom_right));
}

void drawing::rect_gradient_multi(int x1, int y1, int x2, int y2, const s_colour& col_top_left, const s_colour& col_top_right, const s_colour& col_bottom_left, const s_colour& col_bottom_right, bool filled) {
	rect_gradient_multi(s_point(x1, y1), s_point(x2, y2), col_top_left, col_top_right, col_bottom_right, col_bottom_left, filled);
}

void drawing::quadrilateral(const s_point& bottom_left, const s_point& bottom_right, const s_point& top_left, const s_point& top_right, const s_colour& col, bool filled, float thickness) {
	ImVec2 positions[5] = {
		top_left,
		bottom_left,
		bottom_right,
		top_right,
		top_left
	};

	if (filled)
		imgui.drawlist->AddConvexPolyFilled(positions, 5, col.convert());
	else
		imgui.drawlist->AddPolyline(positions, 5, col.convert(), ImDrawFlags_Closed, thickness);
}

void drawing::triangle(const s_point& p1, const s_point& p2, const s_point& p3, const s_colour& col, bool filled, float thickness, bool anti_aliased) {
	if (filled)
		imgui.drawlist->AddTriangleFilled(adjust_position(p1), adjust_position(p2), adjust_position(p3), col.convert());
	else
		imgui.drawlist->AddTriangle(adjust_position(p1), adjust_position(p2), adjust_position(p3), col.convert(), thickness);
}

void drawing::circle(const s_point& pos, float radius, const s_colour& s_colour, bool filled, float thickness, int parts, float degrees, float start_degree, bool antialiased) {
	if (filled)
		imgui.drawlist->AddCircleFilled(adjust_position(pos), radius, s_colour.convert(), parts);
	else {
		if (!(degrees == 360.f && start_degree == 0.f)) {
			auto min_rad = deg_to_rad(start_degree);
			auto max_rad = deg_to_rad(degrees);
			imgui.drawlist->PathArcTo(adjust_position(pos), radius, min_rad, max_rad, parts);
			imgui.drawlist->PathStroke(s_colour.convert(), 0, thickness);
			return;
		}

		imgui.drawlist->AddCircle(adjust_position(pos), radius, s_colour.convert(), parts, thickness);
	}
}

void drawing::string(s_point pos, int flags, const s_colour& colour, const font& font, const std::string& text) {
	if (!font)
		return;

	const auto text_char = text.data();
	bool outline = false;

	ImGui::PushFont(font.im_font());

	if (flags) {
		const auto size = font.calc_size(text);

		if (flags & FONT_CENTERED_X)
			pos.x -= int(size.w * 0.5f);

		if (flags & FONT_CENTERED_Y)
			pos.y -= int(size.h * 0.5f);

		if (flags & FONT_RIGHT_ALIGN)
			pos.x -= size.w;

		if (flags & FONT_BOTTOM_ALIGN)
			pos.y -= size.h;

		if (flags & FONT_OUTLINE)
			outline = true;

		if (flags & FONT_DROPSHADOW) {
			const s_colour dropshadow_colour(0, 0, 0, colour.a * 0.6f);
			const int shift_amount = outline ? 2 : 1;

			imgui.drawlist->AddText(font.im_font(), font.size(), pos.offset(shift_amount), dropshadow_colour.convert(), text.data());
		}
	}

	imgui.drawlist->AddText(font.im_font(), font.size(), pos, colour.convert(), text.data());

	ImGui::PopFont();
}

void drawing::string(s_point pos, int flags, const s_colour& colour, const font& font, const std::wstring& text) {
	return string(pos, flags, colour, font, helpers::tostring(text));
}

void drawing::push_clip_rect(const s_rect& rect, bool intersect_clip_rect) {
	imgui.drawlist->PushClipRect(adjust_position(rect.pos()), adjust_position(rect.max()), intersect_clip_rect);
}

void drawing::push_clip_rect(int x1, int y1, int x2, int y2, bool intersect_clip_rect) {
	push_clip_rect(s_rect(s_point(x1, y1), s_point(x2, y2)), intersect_clip_rect);
}

void drawing::push_fullscreen_clip_rect() {
	push_clip_rect(0, 0, w, h, false);
}

s_rect drawing::pop_clip_rect() {
	auto current = imgui.drawlist->GetClipRect();
	imgui.drawlist->PopClipRect();
	return current;
}

s_rect drawing::get_clip_rect() {
	return imgui.drawlist->GetClipRect();
}

void drawing::push_alpha(float alpha) {
	alphas.push(alpha);
}

void drawing::push_relative_alpha(float alpha) {
	if (alphas.empty())
		alphas.push(alpha);
	else
		alphas.push(alphas.top() * alpha);
}

void drawing::pop_alpha() {
	alphas.pop();
}

std::optional<float> drawing::get_alpha() {
	if (alphas.empty())
		return {};

	return alphas.top();
}

void drawing::push_transform(s_point transform) {
	transforms.push(transform);
}

void drawing::push_relative_transform(s_point transform) {
	if (transforms.empty())
		transforms.push(transform);
	else
		transforms.push(transforms.top() + transform);
}

void drawing::pop_transform() {
	transforms.pop();
}

std::optional<s_point> drawing::get_transform() {
	if (transforms.empty())
		return {};

	return transforms.top();
}

bool drawing::clip_string(std::string& text, const font& font, int max_width, int min_chars) {
	int size = font.calc_size(text).w;
	if (size <= max_width)
		return true;

	// bool start_right = size < max_width * 2;

	int dots_size = font.calc_size("...").w;

	const int num = static_cast<int>(text.size()) - min_chars;
	for (int i = 0; i < num; i++) {
		text.pop_back();

		if (font.calc_size(text).w + dots_size <= max_width) {
			text += "...";
			return true;
		}
	}

	text += "...";
	return false;
}

void drawing::animate(float& val, float goal, float speed) {
	if (val == goal)
		return;

	float fixed_speed = std::clamp(speed * frametime, 0.f, 1.f);
	val = std::lerp(val, goal, fixed_speed);
}

void drawing::round(float& val, float goal, float round_diff) {
	if (val == goal)
		return;

	if (abs(goal - val) <= round_diff)
		val = goal;
}