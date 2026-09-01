#include "render.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>
#include <misc/freetype/imgui_freetype.h>
#include <imgui_internal.h>

#include "../fonts/dejavu_sans.h"
#include "../fonts/nv_garamond.h"
#include "../fonts/icons.h"

namespace {
	gfx::Color interpolate_color(const std::vector<gfx::Color>& colors, const std::vector<float>& positions, float t) {
		// Find the segment containing t
		size_t i = 0;
		for (; i < positions.size() - 1; i++) {
			if (t >= positions[i] && t <= positions[i + 1])
				break;
		}

		// If t is outside the range, clamp to the nearest color
		if (i >= positions.size() - 1) {
			if (t < positions[0])
				return colors[0];
			return colors[colors.size() - 1];
		}

		// Normalize t to the segment
		float segment_t = (t - positions[i]) / (positions[i + 1] - positions[i]);

		// Linear interpolation between colors
		const gfx::Color& c1 = colors[i];
		const gfx::Color& c2 = colors[i + 1];

		return {
			static_cast<uint8_t>(c1.r + (segment_t * (c2.r - c1.r))),
			static_cast<uint8_t>(c1.g + (segment_t * (c2.g - c1.g))),
			static_cast<uint8_t>(c1.b + (segment_t * (c2.b - c1.b))),
			static_cast<uint8_t>(c1.a + (segment_t * (c2.a - c1.a))),
		};
	}
}

bool render::ImGuiWrap::init(SDL_Window* window, const SDL_GLContext& context) {
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ctx = ImGui::CreateContext();
	io = &ImGui::GetIO();
	io->IniFilename = nullptr;
	io->LogFilename = nullptr;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// Setup platform/renderer backends for ANGLE's OpenGL ES 3 context.
	ImGui_ImplSDL3_InitForOpenGL(window, context);
	ImGui_ImplOpenGL3_Init("#version 300 es");

	return true;
}

bool render::init(SDL_Window* window, const SDL_GLContext& context) {
	if (!imgui.init(window, context))
		return false;

	ImFontConfig font_cfg;

	// init fonts
	if (!fonts::dejavu.init(DEJAVU_SANS_COMPRESSED_DATA, fonts::size::BODY, &font_cfg))
		return false;

	if (!fonts::garamond.init(NV_GARAMOND_COMPRESSED_DATA, fonts::size::HEADER, &font_cfg))
		return false;

	if (!fonts::icons.init(ICONS_COMPRESSED_DATA, fonts::size::ICON, &font_cfg))
		return false;

	initialised = true;

	return true;
}

void render::destroy() {
	initialised = false;

	if (!imgui.ctx)
		return;

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	imgui.ctx = nullptr;
	imgui.io = nullptr;
	imgui.drawlist = nullptr;
}

float render::get_content_scale(SDL_Window* window) {
	if (dpi_scale_override > 0.f)
		return dpi_scale_override;

	// SDL_GetWindowDisplayScale combines the retina pixel density with the os content scale.
	// we only want the content scale part (imgui/sdl handle pixel density on their own), so divide it out.
	float display_scale = SDL_GetWindowDisplayScale(window);
	float pixel_density = SDL_GetWindowPixelDensity(window);

	if (display_scale <= 0.f)
		return 1.f;

	float content_scale = pixel_density > 0.f ? display_scale / pixel_density : display_scale;

	// never shrink the ui below the design size
	return std::max(content_scale, 1.f);
}

void render::update_window_size(SDL_Window* window) {
	ui_scale = get_content_scale(window);

	int window_w = 0;
	int window_h = 0;
	SDL_GetWindowSize(window, &window_w, &window_h);

	// lay out in scaled (logical) units so everything grows with the os content scale
	window_size.w = (int)std::lround((float)window_w / ui_scale);
	window_size.h = (int)std::lround((float)window_h / ui_scale);
}

void render::ImGuiWrap::begin(SDL_Window* window) {
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();

	// apply our extra content/dpi scaling on top of what the sdl backend set up.
	// window_size (set in update_window_size) is our scaled-down logical layout size, so tell imgui to
	// project it onto the full framebuffer. this makes everything ui_scale times bigger and, because
	// imgui rasterises dynamic fonts at DisplaySize * DisplayFramebufferScale, keeps text crisp.
	// must happen before ImGui::NewFrame(), which consumes io.DisplaySize.
	{
		int pixel_w = 0;
		int pixel_h = 0;
		SDL_GetWindowSizeInPixels(window, &pixel_w, &pixel_h);

		io->DisplaySize = ImVec2((float)window_size.w, (float)window_size.h);
		io->DisplayFramebufferScale = ImVec2(
			window_size.w > 0 ? (float)pixel_w / (float)window_size.w : 1.f,
			window_size.h > 0 ? (float)pixel_h / (float)window_size.h : 1.f
		);

		framebuffer_scale = std::max(io->DisplayFramebufferScale.x, io->DisplayFramebufferScale.y);
	}

	ImGui::NewFrame();

	drawlist = ImGui::GetForegroundDrawList();

	// calculate frametime
	auto now = std::chrono::high_resolution_clock::now();
	static auto last = now;

	frametime = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(now - last).count() / 1000.f;

	last = now;
}

void render::ImGuiWrap::end(SDL_Window* window) { // NOLINT(readability-convert-member-functions-to-static)
	                                              // ^ yeah, but this is nicer to call
	static constexpr ImVec4 clear_colour = ImVec4(0.f, 0.f, 0.f, 1.f);

	for (auto& call : late_draw_calls) {
		call();
	}
	late_draw_calls.clear();

	ImGui::Render();

	int drawable_width = 0;
	int drawable_height = 0;
	SDL_GetWindowSizeInPixels(window, &drawable_width, &drawable_height);

	glViewport(0, 0, drawable_width, drawable_height);

	glClearColor(
		clear_colour.x * clear_colour.w,
		clear_colour.y * clear_colour.w,
		clear_colour.z * clear_colour.w,
		clear_colour.w
	);
	glClear(GL_COLOR_BUFFER_BIT);

	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	SDL_GL_SwapWindow(window);
}

void render::line(
	const gfx::Point& pos1, const gfx::Point& pos2, const gfx::Color& col, bool anti_aliased, float thickness
) {
	imgui.drawlist->AddLine(pos1, pos2, col.to_imgui(), thickness);
}

void render::rect_filled(const gfx::Rect& rect, const gfx::Color& col) {
	imgui.drawlist->AddRectFilled(rect.origin(), rect.max(), col.to_imgui());
}

void render::rect_stroke(const gfx::Rect& rect, const gfx::Color& col, float thickness) {
	imgui.drawlist->AddRect(rect.origin(), rect.max(), col.to_imgui(), 0.f, 0, thickness);
}

void render::rounded_rect_filled(
	const gfx::Rect& rect, const gfx::Color& col, float rounding, unsigned int rounding_flags
) {
	imgui.drawlist->AddRectFilled(rect.origin(), rect.max(), col.to_imgui(), rounding, rounding_flags);
}

void render::rounded_rect_stroke(
	const gfx::Rect& rect, const gfx::Color& col, float rounding, unsigned int rounding_flags, float thickness
) {
	imgui.drawlist->AddRect(rect.origin(), rect.max(), col.to_imgui(), rounding, rounding_flags, thickness);
}

void render::rect_filled_gradient(
	const gfx::Rect& rect,
	const std::vector<gfx::Point>& gradient_direction,
	const std::vector<gfx::Color>& colors,
	const std::vector<float>& positions
) {
	// Validate inputs
	if (colors.size() < 2 || colors.size() != positions.size() || gradient_direction.size() != 2) {
		return; // Invalid input parameters
	}

	// For custom gradient direction, we need to split the rectangle into multiple triangles
	ImVec2 origin = rect.origin();
	ImVec2 max = rect.max();

	// Calculate direction vector
	ImVec2 dir_vec(
		gradient_direction[1].x - gradient_direction[0].x, gradient_direction[1].y - gradient_direction[0].y
	);
	float dir_length = sqrtf((dir_vec.x * dir_vec.x) + (dir_vec.y * dir_vec.y));

	if (dir_length < 0.0001f) {
		return; // Invalid direction
	}

	// Normalize direction vector
	dir_vec.x /= dir_length;
	dir_vec.y /= dir_length;

	// Calculate perpendicular vector
	ImVec2 perp_vec(-dir_vec.y, dir_vec.x);

	// Create gradient mesh using triangles
	const int segments = 32; // Number of segments for smooth gradient

	for (int i = 0; i < segments; i++) {
		float t1 = static_cast<float>(i) / segments;
		float t2 = static_cast<float>(i + 1) / segments;

		// Find color at position t1 and t2
		ImU32 col1 = interpolate_color(colors, positions, t1).to_imgui();
		ImU32 col2 = interpolate_color(colors, positions, t2).to_imgui();

		// Calculate points for this segment
		ImVec2 p1 = ImVec2(
			origin.x + (dir_vec.x * t1 * (max.x - origin.x)) + (perp_vec.x * origin.y),
			origin.y + (dir_vec.y * t1 * (max.y - origin.y)) + (perp_vec.y * origin.x)
		);
		ImVec2 p2 = ImVec2(
			origin.x + (dir_vec.x * t2 * (max.x - origin.x)) + (perp_vec.x * origin.y),
			origin.y + (dir_vec.y * t2 * (max.y - origin.y)) + (perp_vec.y * origin.x)
		);
		ImVec2 p3 = ImVec2(
			max.x + (dir_vec.x * t1 * (max.x - origin.x)) + (perp_vec.x * max.y),
			max.y + (dir_vec.y * t1 * (max.y - origin.y)) + (perp_vec.y * max.x)
		);
		ImVec2 p4 = ImVec2(
			max.x + (dir_vec.x * t2 * (max.x - origin.x)) + (perp_vec.x * max.y),
			max.y + (dir_vec.y * t2 * (max.y - origin.y)) + (perp_vec.y * max.x)
		);

		// Draw triangles
		imgui.drawlist->AddTriangleFilled(p1, p2, p3, col1);
		imgui.drawlist->AddTriangleFilled(p2, p3, p4, col2);
	}
}

void render::rect_filled_gradient(
	const gfx::Rect& rect,
	GradientDirection direction,
	const std::vector<gfx::Color>& colors,
	const std::vector<float>& positions
) {
	// Validate inputs
	if (colors.size() < 2 || colors.size() != positions.size()) {
		return; // Invalid input parameters
	}

	ImVec2 origin = rect.origin();
	ImVec2 max = rect.max();

	// For predefined directions, we can use the ImGui built-in functions
	if (colors.size() == 2) {
		ImU32 col1 = colors[0].to_imgui();
		ImU32 col2 = colors[1].to_imgui();

		switch (direction) {
			case GradientDirection::GRADIENT_RIGHT:
				imgui.drawlist->AddRectFilledMultiColor(origin, max, col1, col2, col2, col1);
				break;
			case GradientDirection::GRADIENT_LEFT:
				imgui.drawlist->AddRectFilledMultiColor(origin, max, col2, col1, col1, col2);
				break;
			case GradientDirection::GRADIENT_DOWN:
				imgui.drawlist->AddRectFilledMultiColor(origin, max, col1, col1, col2, col2);
				break;
			case GradientDirection::GRADIENT_UP:
				imgui.drawlist->AddRectFilledMultiColor(origin, max, col2, col2, col1, col1);
				break;
			case GradientDirection::GRADIENT_DOWN_RIGHT:
				imgui.drawlist->AddRectFilledMultiColor(origin, max, col1, col2, col2, col1);
				break;
			case GradientDirection::GRADIENT_UP_LEFT:
				imgui.drawlist->AddRectFilledMultiColor(origin, max, col2, col1, col1, col2);
				break;
			case GradientDirection::GRADIENT_DOWN_LEFT:
				imgui.drawlist->AddRectFilledMultiColor(origin, max, col2, col1, col2, col1);
				break;
			case GradientDirection::GRADIENT_UP_RIGHT:
				imgui.drawlist->AddRectFilledMultiColor(origin, max, col1, col2, col1, col2);
				break;
		}
	}
	else {
		// For more than 2 colors, we need a more complex approach
		// Convert direction to points for the general implementation
		std::vector<gfx::Point> gradient_direction;

		switch (direction) {
			case GradientDirection::GRADIENT_RIGHT:
				gradient_direction = { gfx::Point(origin.x, origin.y), gfx::Point(max.x, origin.y) };
				break;
			case GradientDirection::GRADIENT_LEFT:
				gradient_direction = { gfx::Point(max.x, origin.y), gfx::Point(origin.x, origin.y) };
				break;
			case GradientDirection::GRADIENT_DOWN:
				gradient_direction = { gfx::Point(origin.x, origin.y), gfx::Point(origin.x, max.y) };
				break;
			case GradientDirection::GRADIENT_UP:
				gradient_direction = { gfx::Point(origin.x, max.y), gfx::Point(origin.x, origin.y) };
				break;
			case GradientDirection::GRADIENT_DOWN_RIGHT:
				gradient_direction = { gfx::Point(origin.x, origin.y), gfx::Point(max.x, max.y) };
				break;
			case GradientDirection::GRADIENT_UP_LEFT:
				gradient_direction = { gfx::Point(max.x, max.y), gfx::Point(origin.x, origin.y) };
				break;
			case GradientDirection::GRADIENT_DOWN_LEFT:
				gradient_direction = { gfx::Point(max.x, origin.y), gfx::Point(origin.x, max.y) };
				break;
			case GradientDirection::GRADIENT_UP_RIGHT:
				gradient_direction = { gfx::Point(origin.x, max.y), gfx::Point(max.x, origin.y) };
				break;
		}

		// Use the general implementation
		rect_filled_gradient(rect, gradient_direction, colors, positions);
	}
}

void render::rect_gradient_multi_filled(
	const gfx::Rect& rect,
	const gfx::Color& col_top_left,
	const gfx::Color& col_top_right,
	const gfx::Color& col_bottom_left,
	const gfx::Color& col_bottom_right
) {
	render::rect_stroke(rect, col_top_left);

	imgui.drawlist->AddRectFilledMultiColor(
		rect.top_left(),
		rect.bottom_right(),
		col_top_left.to_imgui(),
		col_top_right.to_imgui(),
		col_bottom_right.to_imgui(),
		col_bottom_left.to_imgui()
	);
}

void render::quadrilateral_filled(
	const gfx::Point& bottom_left,
	const gfx::Point& bottom_right,
	const gfx::Point& top_left,
	const gfx::Point& top_right,
	const gfx::Color& col
) {
	std::array<ImVec2, 5> positions = { top_left, bottom_left, bottom_right, top_right, top_left };

	imgui.drawlist->AddConvexPolyFilled(positions.data(), static_cast<int>(positions.size()), col.to_imgui());
}

void render::quadrilateral_stroke(
	const gfx::Point& bottom_left,
	const gfx::Point& bottom_right,
	const gfx::Point& top_left,
	const gfx::Point& top_right,
	const gfx::Color& col,
	float thickness
) {
	std::array<ImVec2, 5> positions = { top_left, bottom_left, bottom_right, top_right, top_left };

	imgui.drawlist->AddPolyline(
		positions.data(), static_cast<int>(positions.size()), col.to_imgui(), ImDrawFlags_Closed, thickness
	);
}

void render::triangle_filled(const gfx::Point& p1, const gfx::Point& p2, const gfx::Point& p3, const gfx::Color& col) {
	imgui.drawlist->AddTriangleFilled(p1, p2, p3, col.to_imgui());
}

void render::triangle_stroke(
	const gfx::Point& p1,
	const gfx::Point& p2,
	const gfx::Point& p3,
	const gfx::Color& col,
	float thickness,
	bool anti_aliased
) {
	imgui.drawlist->AddTriangle(p1, p2, p3, col.to_imgui(), thickness);
}

void render::circle_filled(
	const gfx::Point& pos,
	float radius,
	const gfx::Color& colour,
	float thickness,
	int parts,
	float degrees,
	float start_degree,
	bool anti_aliased
) {
	imgui.drawlist->AddCircleFilled(pos, radius, colour.to_imgui(), parts);
}

void render::circle_stroke(
	const gfx::Point& pos,
	float radius,
	const gfx::Color& colour,
	float thickness,
	int parts,
	float degrees,
	float start_degree,
	bool anti_aliased
) {
	if (degrees != 360.f || start_degree != 0.f) {
		auto min_rad = u::deg_to_rad(start_degree);
		auto max_rad = u::deg_to_rad(degrees);
		imgui.drawlist->PathArcTo(pos, radius, min_rad, max_rad, parts);
		imgui.drawlist->PathStroke(colour.to_imgui(), 0, thickness);
		return;
	}

	imgui.drawlist->AddCircle(pos, radius, colour.to_imgui(), parts, thickness);
}

void render::text(
	gfx::Point pos,
	const gfx::Color& colour,
	const std::string& text,
	const Font& font,
	unsigned int flags,
	float rotation_deg
) {
	if (!font)
		return;

	ImGui::PushFont(font.im_font(), font.size());

	int vtx_idx_begin = imgui.drawlist->VtxBuffer.Size;

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

		if (flags & FONT_OUTLINE) {
			const gfx::Color outline_colour(0, 0, 0, colour.a * 0.8f);
			static const std::array<gfx::Point, 4> offsets = {
				gfx::Point{ -1, 0 },
				gfx::Point{ 1, 0 },
				gfx::Point{ 0, -1 },
				gfx::Point{ 0, 1 },
			};

			for (const auto& offset : offsets) {
				imgui.drawlist->AddText(
					font.im_font(), font.size(), pos + offset, outline_colour.to_imgui(), text.data()
				);
			}
		}

		if (flags & FONT_DROPSHADOW) {
			const gfx::Color dropshadow_colour(0, 0, 0, colour.a * 0.6f);
			const int shift_amount = 1;

			imgui.drawlist->AddText(
				font.im_font(), font.size(), pos.offset(shift_amount), dropshadow_colour.to_imgui(), text.data()
			);
		}
	}

	imgui.drawlist->AddText(font.im_font(), font.size(), pos, colour.to_imgui(), text.data());

	if (rotation_deg != 0.f) {
		int vtx_idx_end = imgui.drawlist->VtxBuffer.Size;
		if (vtx_idx_begin == vtx_idx_end) {
			ImGui::PopFont();
			return;
		}

		// use the center of the vertices that were actually drawn
		float min_x = imgui.drawlist->VtxBuffer[vtx_idx_begin].pos.x;
		float max_x = min_x;
		float min_y = imgui.drawlist->VtxBuffer[vtx_idx_begin].pos.y;
		float max_y = min_y;
		for (int i = vtx_idx_begin + 1; i < vtx_idx_end; i++) {
			min_x = std::min(min_x, imgui.drawlist->VtxBuffer[i].pos.x);
			max_x = std::max(max_x, imgui.drawlist->VtxBuffer[i].pos.x);
			min_y = std::min(min_y, imgui.drawlist->VtxBuffer[i].pos.y);
			max_y = std::max(max_y, imgui.drawlist->VtxBuffer[i].pos.y);
		}

		ImVec2 pivot((min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f);

		float ang = u::deg_to_rad(rotation_deg);
		ImGui::ShadeVertsTransformPos(
			imgui.drawlist, vtx_idx_begin, vtx_idx_end, pivot, std::cos(ang), std::sin(ang), pivot
		);
	}

	ImGui::PopFont();
}

render::Texture::~Texture() {
	destroy();
}

bool render::Texture::load_from_file(const std::string& path) {
	// Use SDL to load the image
	SDL_Surface* surface = IMG_Load(path.c_str());

	if (!surface) {
		u::log_error("Failed to load image: {}", SDL_GetError());
		return false;
	}

	// Convert to RGBA format if needed
	SDL_Surface* rgb_surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(surface);

	if (!rgb_surface) {
		u::log_error("Failed to convert surface: {}", SDL_GetError());
		return false;
	}

	bool result = load_from_surface(rgb_surface);
	SDL_DestroySurface(rgb_surface);

	return result;
}

bool render::Texture::load_from_surface(SDL_Surface* surface) {
	if (!surface)
		return false;

	// Delete old texture if exists
	destroy();

	// Create a new texture
	glGenTextures(1, &m_id);
	glBindTexture(GL_TEXTURE_2D, m_id);

	// Setup texture parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// Upload pixels into texture
	GLenum format = GL_RGBA;
	glTexImage2D(GL_TEXTURE_2D, 0, format, surface->w, surface->h, 0, format, GL_UNSIGNED_BYTE, surface->pixels);

	m_width = surface->w;
	m_height = surface->h;

	glBindTexture(GL_TEXTURE_2D, 0);

	return true;
}

void render::Texture::destroy() {
	if (m_id) {
		glDeleteTextures(1, &m_id);
		m_id = 0;
		m_width = 0;
		m_height = 0;
	}
}

void render::Texture::bind() const {
	glBindTexture(GL_TEXTURE_2D, m_id);
}

void render::Texture::unbind() {
	glBindTexture(GL_TEXTURE_2D, 0);
}

// Image rendering functions
void render::image(const gfx::Rect& rect, const Texture& texture, const gfx::Color& tint_color) {
	if (!texture.is_valid())
		return;

	imgui.drawlist->AddImage(
		texture.get_id(), rect.origin(), rect.max(), ImVec2(0, 0), ImVec2(1, 1), tint_color.to_imgui()
	);
}

void render::image_rounded(
	const gfx::Rect& rect,
	const Texture& texture,
	float rounding,
	unsigned int rounding_flags,
	const gfx::Color& tint_color
) {
	if (!texture.is_valid())
		return;

	imgui.drawlist->AddImageRounded(
		texture.get_id(),
		rect.origin(),
		rect.max(),
		ImVec2(0, 0),
		ImVec2(1, 1),
		tint_color.to_imgui(),
		rounding,
		rounding_flags
	);
}

void render::rounded_image_with_borders(
	const gfx::Rect& rect,
	const Texture& texture,
	float rounding,
	const gfx::Color& border_color,
	const gfx::Color& inner_border_color,
	float border_thickness,
	unsigned int rounding_flags,
	const gfx::Color& tint_color
) {
	if (!texture.is_valid())
		return;

	image_rounded(rect.shrink(3), texture, rounding, rounding_flags, tint_color);

	rounded_rect_stroke(rect.shrink(2), border_color, rounding, rounding_flags, border_thickness);
	rounded_rect_stroke(rect.shrink(1), inner_border_color, rounding, rounding_flags, border_thickness);
	rounded_rect_stroke(rect, border_color, rounding, rounding_flags, border_thickness);
}

void render::borders(const gfx::Rect& rect, const gfx::Color& border_color, const gfx::Color& inner_border_color) {
	rect_stroke(rect.shrink(2), border_color, 1.f);
	rect_stroke(rect.shrink(1), inner_border_color, 1.f);
	rect_stroke(rect, border_color, 1.f);
}

void render::spinner(
	const gfx::Point& pos,
	float radius,
	const gfx::Color& background_color,
	const gfx::Color& highlight_color,
	float thickness,
	float alpha,
	float trail_degrees
) {
	constexpr float SPIN_DEGREES_PER_SECOND = 320.f;
	constexpr int SPIN_SEGMENTS = 32;

	ImVec2 center = pos;

	// background ring
	circle_stroke(pos, radius, background_color.adjust_alpha(alpha), thickness, SPIN_SEGMENTS);

	// head ring
	float head_degree = std::fmod((float)ImGui::GetTime() * SPIN_DEGREES_PER_SECOND, 360.f);
	float tail_degree = head_degree - trail_degrees;

	for (int i = 0; i < SPIN_SEGMENTS; i++) {
		float segment_start_fraction = (float)i / SPIN_SEGMENTS;
		float segment_end_fraction = (float)(i + 1) / SPIN_SEGMENTS;

		float segment_start_angle = u::deg_to_rad(tail_degree + trail_degrees * segment_start_fraction);
		float segment_end_angle = u::deg_to_rad(tail_degree + trail_degrees * segment_end_fraction);

		ImVec2 segment_start_pos(
			center.x + std::cos(segment_start_angle) * radius, center.y + std::sin(segment_start_angle) * radius
		);
		ImVec2 segment_end_pos(
			center.x + std::cos(segment_end_angle) * radius, center.y + std::sin(segment_end_angle) * radius
		);

		gfx::Color segment_color = highlight_color.adjust_alpha(segment_end_fraction * alpha);

		// note: directly calling AddLine for float precision
		imgui.drawlist->AddLine(segment_start_pos, segment_end_pos, segment_color.to_imgui(), thickness);
	}
}

static gfx::Point catmull_rom(
	const gfx::Point& p0, const gfx::Point& p1, const gfx::Point& p2, const gfx::Point& p3, float t
) {
	float t2 = t * t;
	float t3 = t2 * t;

	float x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
	                  (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);

	float y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
	                  (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);

	return { (int)x, (int)y };
}

void render::waveform(
	const gfx::Rect& rect,
	const gfx::Rect& active_rect,
	const gfx::Color& color,
	const std::vector<int16_t>& samples,
	int16_t max_sample,
	float zoom_start,
	float zoom_end
) {
	if (samples.empty() || max_sample <= 0 || rect.w <= 1)
		return;

	zoom_start = std::clamp(zoom_start, 0.0f, 1.0f);
	zoom_end = std::clamp(zoom_end, 0.0f, 1.0f);
	if (zoom_start >= zoom_end)
		return;

	const int width = rect.w;
	const int height = rect.h;
	const int y_center = rect.y + height / 2;
	const float scale = height * 0.5f;

	const size_t total_samples = samples.size();
	const size_t start_idx = static_cast<size_t>(zoom_start * total_samples);
	const size_t end_idx = std::min(static_cast<size_t>(zoom_end * total_samples), total_samples);

	if (start_idx >= end_idx)
		return;

	const size_t sample_range = end_idx - start_idx;
	const float samples_per_pixel = static_cast<float>(sample_range) / width;

	int16_t display_max = u::get_audio_percentile_peak(samples, 1.f); // 0.999f);

	if (samples_per_pixel >= 2.0f) {
		// Zoomed out: draw amplitude envelope
		for (int x = 0; x < width; ++x) {
			const size_t pixel_start = start_idx + static_cast<size_t>(x * samples_per_pixel);
			const size_t pixel_end = std::min(start_idx + static_cast<size_t>((x + 1) * samples_per_pixel), end_idx);

			// Find peak amplitude in this pixel range
			float peak_amplitude = 0.0f;
			for (size_t i = pixel_start; i < pixel_end; ++i) {
				float amplitude = std::abs(static_cast<float>(samples[i])) / display_max;
				amplitude = std::min(amplitude, 1.0f);
				peak_amplitude = std::max(peak_amplitude, amplitude);
			}

			// Draw symmetric line above and below center
			const int amplitude_height = static_cast<int>(peak_amplitude * scale);

			if (amplitude_height > 0) {
				const int y_top = y_center - amplitude_height;
				const int y_bottom = y_center + amplitude_height;

				const gfx::Point p1{ rect.x + x, y_top };
				const gfx::Point p2{ rect.x + x, y_bottom };

				auto line_color = color;
				if (!active_rect.contains(p1) && !active_rect.contains(p2)) {
					line_color = line_color.adjust_alpha(0.5f);
				}

				line(p1, p2, line_color, true, 1.0f);
			}
			else {
				gfx::Point point{ rect.x + x, y_center };

				auto point_color = color;
				if (!active_rect.contains(point)) {
					point_color = point_color.adjust_alpha(0.5f);
				}

				rect_filled(gfx::Rect(point, gfx::Size(1, 1)), point_color);
			}
		}
	}
	else {
		// Zoomed in: draw smooth interpolated curve
		std::vector<gfx::Point> points;
		points.reserve(sample_range);

		for (size_t i = start_idx; i < end_idx; ++i) {
			float amplitude = std::abs(static_cast<float>(samples[i])) / display_max;
			amplitude = std::min(amplitude, 1.0f);

			const int x = rect.x + static_cast<int>((i - start_idx) * width / static_cast<float>(sample_range));

			// Alternate above/below center based on sample index
			const bool draw_above = (i % 2 == 0);
			const int y = draw_above ? y_center - static_cast<int>(amplitude * scale)  // Above center
			                         : y_center + static_cast<int>(amplitude * scale); // Below center

			points.push_back({ x, y });
		}

		if (points.size() >= 4) {
			// Draw Catmull-Rom spline
			// TODO MR: really small amplitudes still drawing 2 pixels high line? copy zoomed out rect thingy to make it
			// 1 pixel?
			for (size_t i = 1; i + 2 < points.size(); ++i) {
				const gfx::Point& p0 = points[i - 1];
				const gfx::Point& p1 = points[i];
				const gfx::Point& p2 = points[i + 1];
				const gfx::Point& p3 = points[i + 2];

				auto segment_color = color;
				if (!active_rect.contains(p1) && !active_rect.contains(p2)) {
					segment_color = segment_color.adjust_alpha(0.5f);
				}

				gfx::Point prev = p1;
				for (int j = 1; j <= 12; ++j) {
					const float t = j / 12.0f;
					const gfx::Point current = catmull_rom(p0, p1, p2, p3, t);
					line(prev, current, segment_color, true, 1.5f);
					prev = current;
				}
			}
		}
		else if (points.size() >= 2) {
			for (size_t i = 1; i < points.size(); ++i) {
				auto segment_color = color;
				if (!active_rect.contains(points[i - 1]) && !active_rect.contains(points[i])) {
					segment_color = segment_color.adjust_alpha(0.5f);
				}
				line(points[i - 1], points[i], segment_color, true, 1.5f);
			}
		}
	}
}

void render::rect_side(const gfx::Rect& rect, const gfx::Color& color, RectSide side, int thickness) {
	switch (side) {
		case RectSide::LEFT: {
			// Top horizontal
			rect_filled(gfx::Rect{ rect.x, rect.y - thickness, rect.w, thickness }, color);
			// Vertical
			rect_filled(
				gfx::Rect{ rect.x - thickness, rect.y - thickness, thickness, rect.h + (thickness * 2) }, color
			);
			// Bottom horizontal
			rect_filled(gfx::Rect{ rect.x, rect.y + rect.h, rect.w, thickness }, color);
			break;
		}
		case RectSide::RIGHT: {
			// Top horizontal
			rect_filled(gfx::Rect{ rect.x, rect.y - thickness, rect.w, thickness }, color);
			// Vertical
			rect_filled(gfx::Rect{ rect.x + rect.w, rect.y - thickness, thickness, rect.h + (thickness * 2) }, color);
			// Bottom horizontal
			rect_filled(gfx::Rect{ rect.x, rect.y + rect.h, rect.w, thickness }, color);
			break;
		}
	}
}

void render::push_clip_rect(const gfx::Rect& rect, bool intersect_clip_rect) {
	imgui.drawlist->PushClipRect(rect.origin(), rect.max(), intersect_clip_rect);
}

void render::push_clip_rect(int x1, int y1, int x2, int y2, bool intersect_clip_rect) {
	push_clip_rect(gfx::Rect(gfx::Point(x1, y1), gfx::Point(x2, y2)), intersect_clip_rect);
}

void render::push_fullscreen_clip_rect() {
	push_clip_rect(0, 0, window_size.w, window_size.h, false);
}

gfx::Rect render::pop_clip_rect() {
	ImVec2 min = imgui.drawlist->GetClipRectMin();
	ImVec2 max = imgui.drawlist->GetClipRectMax();

	imgui.drawlist->PopClipRect();

	return {
		gfx::Point(min.x, min.y),
		gfx::Point(max.x, max.y),
	};
}

gfx::Rect render::get_clip_rect() {
	ImVec2 min = imgui.drawlist->GetClipRectMin();
	ImVec2 max = imgui.drawlist->GetClipRectMax();

	return {
		gfx::Point(min.x, min.y),
		gfx::Point(max.x, max.y),
	};
}

size_t render::draw_vertex_count() {
	return static_cast<size_t>(imgui.drawlist->VtxBuffer.Size);
}

void render::transform_draw_vertices(size_t first_vertex, const gfx::Rect& from, const gfx::Rect& to, float opacity) {
	if (from.is_empty())
		return;

	opacity = std::clamp(opacity, 0.f, 1.f);

	// settled and fully opaque, so there's nothing to apply. worth checking: this runs over every vertex the
	// caller submitted, and callers hand it the same range every frame whether or not anything is moving
	if (from == to && opacity == 1.f)
		return;

	float scale_x = to.w / static_cast<float>(from.w);
	float scale_y = to.h / static_cast<float>(from.h);

	for (size_t i = first_vertex; i < static_cast<size_t>(imgui.drawlist->VtxBuffer.Size); i++) {
		auto& vertex = imgui.drawlist->VtxBuffer[static_cast<int>(i)];

		vertex.pos.x = to.x + ((vertex.pos.x - from.x) * scale_x);
		vertex.pos.y = to.y + ((vertex.pos.y - from.y) * scale_y);

		uint32_t alpha = (vertex.col >> IM_COL32_A_SHIFT) & 0xff;
		alpha = static_cast<uint32_t>(std::lround(alpha * opacity));
		vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
	}
}

bool render::clip_string(std::string& text, const Font& font, int max_width, int min_chars) {
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

namespace {
	// bytes in the utf-8 sequence starting at c. measuring a lone continuation byte would give the width of the
	// fallback glyph rather than the character it belongs to
	int utf8_seq_len(unsigned char c) {
		if (c < 0x80)
			return 1;
		if ((c & 0xE0) == 0xC0)
			return 2;
		if ((c & 0xF0) == 0xE0)
			return 3;
		if ((c & 0xF8) == 0xF0)
			return 4;
		return 1;
	}
}

std::vector<std::string> render::wrap_text_verbatim(const std::string& text, int max_width, const Font& font) {
	std::vector<std::string> lines;
	if (!font)
		return lines;

	auto limit = static_cast<float>(std::max(max_width, 1));

	size_t line_start = 0;
	while (true) {
		size_t newline = text.find('\n', line_start);
		size_t line_end = newline == std::string::npos ? text.length() : newline;

		size_t start = line_start;
		size_t break_at = std::string::npos; // where we'd rather break than split a word, just past a space
		float width = 0.f;

		size_t i = line_start;
		while (i < line_end) {
			size_t next = std::min(i + utf8_seq_len(static_cast<unsigned char>(text[i])), line_end);
			float advance = font.calc_width(text.data() + i, text.data() + next);

			// i > start keeps a field narrower than a single glyph from looping forever
			if (width + advance > limit && i > start) {
				size_t split = break_at > start && break_at != std::string::npos ? break_at : i;

				lines.push_back(text.substr(start, split - start));
				start = split;
				break_at = std::string::npos;

				// whatever carried onto the new line, which is at most one word
				width = font.calc_width(text.data() + start, text.data() + i);
			}

			width += advance;

			// break after the space, so trailing spaces stay on the line they ended
			if (text[i] == ' ' || text[i] == '\t')
				break_at = next;

			i = next;
		}

		lines.push_back(text.substr(start, line_end - start));

		if (newline == std::string::npos)
			break;

		line_start = newline + 1;
	}

	return lines;
}

std::vector<std::string> render::wrap_text(
	const std::string& text, const gfx::Size& dimensions, const Font& font, int line_height
) {
	std::vector<std::string> lines;
	if (!font)
		return lines;

	std::istringstream iss(text);
	std::string line;

	while (std::getline(iss, line)) {
		std::istringstream line_stream(line);
		std::string word;
		std::string current_line;

		while (line_stream >> word) {
			std::string test_line = current_line;
			if (!test_line.empty())
				test_line += ' ';
			test_line += word;

			if (font.calc_size(test_line).w > dimensions.w) {
				if (!current_line.empty()) {
					lines.push_back(current_line);
					current_line = word;
				}
				else {
					// Word itself is too long, hard break
					std::string sub_word;
					for (char c : word) {
						sub_word += c;
						if (font.calc_size(sub_word).w > dimensions.w) {
							if (sub_word.length() > 1) {
								lines.push_back(sub_word.substr(0, sub_word.length() - 1));
								sub_word = sub_word.back();
							}
						}
					}
					current_line = sub_word;
				}
			}
			else {
				current_line = test_line;
			}
		}

		if (!current_line.empty()) {
			lines.push_back(current_line);
		}
		else {
			lines.push_back("");
		}
	}

	return lines;
}

namespace {
	SDL_Surface* bytes_to_surface(const void* data, size_t size, const char* type) {
		SDL_IOStream* io = SDL_IOFromConstMem(data, size);
		if (!io)
			return nullptr;

		SDL_Surface* surface = IMG_LoadTyped_IO(io, /*closeio=*/true, type);
		if (!surface)
			return nullptr;

		SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
		SDL_DestroySurface(surface);

		return rgba;
	}
}

SDL_Surface* render::jpeg_bytes_to_surface(const void* data, size_t size) {
	return bytes_to_surface(data, size, "JPG");
}

SDL_Surface* render::png_bytes_to_surface(const void* data, size_t size) {
	return bytes_to_surface(data, size, "PNG");
}

std::shared_ptr<render::Texture> render::texture_from_jpeg(std::span<const uint8_t> jpeg) {
	if (jpeg.empty())
		return nullptr;

	SDL_Surface* rgba = jpeg_bytes_to_surface(jpeg.data(), jpeg.size());
	if (!rgba)
		return nullptr;

	auto texture = std::make_shared<Texture>();
	bool loaded = texture->load_from_surface(rgba);

	SDL_DestroySurface(rgba);

	return loaded ? texture : nullptr;
}
