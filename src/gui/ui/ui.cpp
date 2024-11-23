#include "ui.h"
#include "os/draw_text.h"
#include "render.h"

const int gap = 10;

void render_bar(os::Surface* surface, const ui::Element& element, float alpha) {
	auto& bar_data = std::get<ui::BarElementData>(element.data);

	// bar background
	render::rect_filled(surface, element.rect, gfx::seta(bar_data.background_color, alpha * 255));

	// bar fill
	if (bar_data.percent_fill > 0) {
		gfx::Rect fill_rect = element.rect;
		fill_rect.w = static_cast<int>(element.rect.w * bar_data.percent_fill);
		render::rect_filled(surface, fill_rect, gfx::seta(bar_data.fill_color, alpha * 255));
	}
}

void render_text(os::Surface* surface, const ui::Element& element, float alpha) {
	auto& text_data = std::get<ui::TextElementData>(element.data);

	gfx::Point text_pos = element.rect.origin();
	text_pos.y += text_data.font.getSize();

	// {
	// 	gfx::Rect temp_rect = element.rect;
	// 	temp_rect.x -= 6 / 2;
	// 	temp_rect.w += 6;

	// 	render::rect_filled(surface, temp_rect, gfx::rgba(255, 255, 0, 100));
	// }

	render::text(surface, text_pos, gfx::seta(text_data.color, alpha * 255), text_data.text, text_data.font, text_data.align);
}

void render_image(os::Surface* surface, const ui::Element& element, float alpha) {
	auto& image_data = std::get<ui::ImageElementData>(element.data);

	gfx::Rect image_rect = element.rect;
	image_rect.shrink(3);

	surface->drawSurface(image_data.image_surface.get(), image_data.image_surface->bounds(), image_rect);

	os::Paint stroke_paint;
	stroke_paint.style(os::Paint::Style::Stroke);
	stroke_paint.strokeWidth(1);

	image_rect.enlarge(1);
	stroke_paint.color(gfx::rgba(155, 155, 155, 125));
	surface->drawRect(image_rect, stroke_paint);

	image_rect.enlarge(1);
	stroke_paint.color(gfx::rgba(80, 80, 80, 125));
	surface->drawRect(image_rect, stroke_paint);

	image_rect.enlarge(1);
	stroke_paint.color(gfx::rgba(155, 155, 155, 125));
	surface->drawRect(image_rect, stroke_paint);
}

// Create a container
ui::Container ui::create_container(gfx::Rect rect, std::optional<gfx::Color> bg_color) {
	ui::Container container = { rect, bg_color, {} };
	container.current_position = { rect.x + 10, rect.y + 10 }; // Add padding
	return container;
}

void ui::add_element(Container& container, const Element& element) {
	container.current_position.y += element.rect.h + gap;

	container.elements.push_back(element);
}

void ui::add_bar(Container& container, float percent_fill, gfx::Color bg_color, gfx::Color fill_color) {
	BarElementData bar_data = { percent_fill, bg_color, fill_color };
	Element bar_element = {
		ElementType::BAR,
		gfx::Rect(container.current_position, gfx::Size(300, 6)),
		bar_data,
		render_bar
	};

	add_element(container, bar_element);
}

void ui::add_text(Container& container, const std::string& text, gfx::Color color, const SkFont& font, os::TextAlign align) {
	TextElementData text_data = { text, color, font, align };
	Element text_element = {
		ElementType::TEXT,
		gfx::Rect(container.current_position, gfx::Size(0, font.getSize())),
		text_data,
		render_text
	};

	add_element(container, text_element);
}

void ui::add_image(Container& container, std::string image_path, gfx::Size max_size) {
	os::SurfaceRef image_surface = os::instance()->loadRgbaSurface(image_path.c_str()); // todo: dont re-load (or re-render at all) if image hasnt changed

	if (!image_surface)
		return;

	ImageElementData image_data = { image_surface };

	gfx::Rect image_rect(container.current_position, max_size);

	float aspect_ratio = image_surface->width() / static_cast<float>(image_surface->height());

	// calculate dimensions while maintaining the aspect ratio
	float target_width = image_rect.h * aspect_ratio;
	float target_height = image_rect.w / aspect_ratio;

	if (target_width <= image_rect.w) {
		// adjust width to maintain aspect ratio
		image_rect.w = static_cast<int>(target_width);
	}
	else {
		// adjust height to maintain aspect ratio
		image_rect.h = static_cast<int>(target_height);
	}

	// enforce maximum size constraints
	if (image_rect.h > max_size.h) {
		image_rect.h = max_size.h;
		image_rect.w = static_cast<int>(max_size.h * aspect_ratio);
	}

	if (image_rect.w > max_size.w) {
		image_rect.w = max_size.w;
		image_rect.h = static_cast<int>(max_size.w / aspect_ratio);
	}

	// center horizontally
	image_rect.x += (max_size.w - image_rect.w) / 2;

	Element image_element = {
		ElementType::IMAGE,
		image_rect,
		image_data,
		render_image
	};

	add_element(container, image_element);
}

void ui::center_elements_in_container(Container& container, bool horizontal, bool vertical) {
	int total_height = container.current_position.y - container.rect.y;

	// remove the last gap
	total_height -= gap;

	int start_x = container.rect.x;
	int start_y = container.rect.y;

	// calculate the starting y position to center elements vertically
	if (vertical) {
		start_y = (container.rect.h - total_height) / 2 + container.rect.y;
	}

	// calculate the starting x position to center elements horizontally
	if (horizontal) {
		int total_width = 0;
		for (const auto& element : container.elements) {
			total_width = std::max(total_width, element.rect.w);
		}
		start_x = (container.rect.w - total_width) / 2 + container.rect.x;
	}

	// update element positions
	int current_y = start_y;
	for (auto& element : container.elements) {
		element.rect.y = current_y;
		if (horizontal) {
			element.rect.x = container.rect.center().x - element.rect.w / 2;
		}

		current_y += element.rect.h + gap;
	}
}

void ui::render_container(os::Surface* surface, const Container& container, float alpha) {
	if (container.background_color) {
		render::rect_filled(surface, container.rect, gfx::seta(*container.background_color, alpha * 255));
	}

	for (const auto& element : container.elements) {
		element.render_fn(surface, element, alpha);
	}
}
