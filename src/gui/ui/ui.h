#pragma once

#include "gfx/point.h"
#include "gfx/size.h"
#include "gfx/color.h"
#include "os/draw_text.h"
#include "os/font.h"

namespace ui {
	enum class ElementType {
		BAR,
		TEXT,
		IMAGE
	};

	struct BarElementData {
		float percent_fill;
		gfx::Color background_color;
		gfx::Color fill_color;
	};

	struct TextElementData {
		std::string text;
		gfx::Color color;
		SkFont font;
		os::TextAlign align;
	};

	struct ImageElementData {
		os::SurfaceRef image_surface;
	};

	using ElementData = std::variant<BarElementData, TextElementData, ImageElementData>;

	struct Element {
		ElementType type;
		gfx::Rect rect;
		ElementData data;                                                   // Specific element data
		std::function<void(os::Surface*, const Element&, float)> render_fn; // Custom render function
	};

	struct Container {
		gfx::Rect rect;
		std::optional<gfx::Color> background_color;
		gfx::Point current_position;
		std::vector<Element> elements;
	};

	Container create_container(gfx::Rect rect, std::optional<gfx::Color> bg_color = {});
	void add_element(Container& container, const Element& element);

	void add_bar(Container& container, float percent_fill, gfx::Color bg_color, gfx::Color fill_color);
	void add_text(Container& container, const std::string& text, gfx::Color color, const SkFont& font, os::TextAlign align = os::TextAlign::Left);
	void add_image(Container& container, std::string image_path, gfx::Size max_size);

	void center_elements_in_container(Container& container, bool horizontal = true, bool vertical = true);

	void render_container(os::Surface* surface, const Container& container, float alpha);
}
