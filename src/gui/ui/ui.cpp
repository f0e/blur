#include "ui.h"
#include "os/draw_text.h"
#include "os/sampling.h"
#include "render.h"

const int gap = 10;

void ui::render_bar(os::Surface* surface, const Element* element, float anim) {
	auto& bar_data = std::get<BarElementData>(element->data);

	int alpha = anim * 255;
	if (alpha == 0)
		return;

	render::rect_filled(surface, element->rect, gfx::seta(bar_data.background_color, alpha));

	if (bar_data.percent_fill > 0) {
		gfx::Rect fill_rect = element->rect;
		fill_rect.w = static_cast<int>(element->rect.w * bar_data.percent_fill);
		render::rect_filled(surface, fill_rect, gfx::seta(bar_data.fill_color, alpha));
	}
}

void ui::render_text(os::Surface* surface, const Element* element, float anim) {
	auto& text_data = std::get<TextElementData>(element->data);

	int alpha = anim * 255;
	if (alpha == 0)
		return;

	gfx::Point text_pos = element->rect.origin();
	text_pos.y += text_data.font.getSize();

	render::text(surface, text_pos, gfx::seta(text_data.color, alpha), text_data.text, text_data.font, text_data.align);
}

void ui::render_image(os::Surface* surface, const Element* element, float anim) {
	auto& image_data = std::get<ImageElementData>(element->data);

	int alpha = anim * 255;
	int stroke_alpha = anim * 125;
	if (alpha == 0 && stroke_alpha == 0)
		return;

	gfx::Rect image_rect = element->rect;
	image_rect.shrink(3);

	os::Paint paint;
	paint.color(gfx::rgba(255, 255, 255, alpha));
	surface->drawSurface(image_data.image_surface.get(), image_data.image_surface->bounds(), image_rect, os::Sampling(), &paint);

	os::Paint stroke_paint;
	stroke_paint.style(os::Paint::Style::Stroke);
	stroke_paint.strokeWidth(1);

	image_rect.enlarge(1);
	stroke_paint.color(gfx::rgba(155, 155, 155, stroke_alpha));
	surface->drawRect(image_rect, stroke_paint);

	image_rect.enlarge(1);
	stroke_paint.color(gfx::rgba(80, 80, 80, stroke_alpha));
	surface->drawRect(image_rect, stroke_paint);

	image_rect.enlarge(1);
	stroke_paint.color(gfx::rgba(155, 155, 155, stroke_alpha));
	surface->drawRect(image_rect, stroke_paint);
}

void ui::init_container(Container& container, gfx::Rect rect, std::optional<gfx::Color> background_color) {
	container.rect = rect;
	container.background_color = background_color;
	container.current_position = { rect.x + 10, rect.y + 10 };
	container.current_element_ids = {};
}

void ui::add_element(Container& container, const std::string& id, const Element& element) {
	container.current_position.y += element.rect.h + gap;

	auto element_ptr = std::make_shared<Element>(element);

	if (!container.elements.contains(id)) {
		container.elements[id] = {
			element_ptr,
		};
		printf("first added %s\n", id.c_str());
	}
	else {
		auto& container_element = container.elements[id];
		container_element.element = element_ptr;
	}

	container.current_element_ids.push_back(id);
}

void ui::add_bar(const std::string& id, Container& container, float percent_fill, gfx::Color background_color, gfx::Color fill_color) {
	Element element = {
		ElementType::BAR,
		gfx::Rect(container.current_position, gfx::Size(300, 6)),
		BarElementData{ percent_fill, background_color, fill_color },
		render_bar,
	};

	add_element(container, id, element);
}

void ui::add_text(const std::string& id, Container& container, const std::string& text, gfx::Color color, const SkFont& font, os::TextAlign align) {
	Element element = {
		ElementType::TEXT,
		gfx::Rect(container.current_position, gfx::Size(0, font.getSize())),
		TextElementData{ text, color, font, align },
		render_text,
	};

	add_element(container, id, element);
}

void ui::add_image(const std::string& id, Container& container, std::string image_path, gfx::Size max_size) {
	gfx::Rect image_rect(container.current_position, max_size);

	os::SurfaceRef image_surface = os::instance()->loadRgbaSurface(image_path.c_str());
	if (!image_surface)
		return;

	float aspect_ratio = image_surface->width() / static_cast<float>(image_surface->height());

	float target_width = image_rect.h * aspect_ratio;
	float target_height = image_rect.w / aspect_ratio;

	if (target_width <= image_rect.w) {
		image_rect.w = static_cast<int>(target_width);
	}
	else {
		image_rect.h = static_cast<int>(target_height);
	}

	if (image_rect.h > max_size.h) {
		image_rect.h = max_size.h;
		image_rect.w = static_cast<int>(max_size.h * aspect_ratio);
	}

	if (image_rect.w > max_size.w) {
		image_rect.w = max_size.w;
		image_rect.h = static_cast<int>(max_size.w / aspect_ratio);
	}

	Element element = {
		ElementType::IMAGE,
		image_rect,
		ImageElementData{ image_surface },
		render_image,
	};

	add_element(container, id, element);
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
		for (const auto& id : container.current_element_ids) {
			auto& element = container.elements[id].element;

			total_width = std::max(total_width, element->rect.w);
		}
		start_x = (container.rect.w - total_width) / 2 + container.rect.x;
	}

	// update element positions
	int current_y = start_y;
	for (const auto& id : container.current_element_ids) {
		auto& element = container.elements[id].element;

		element->rect.y = current_y;
		if (horizontal) {
			element->rect.x = container.rect.center().x - element->rect.w / 2;
		}

		current_y += element->rect.h + gap;
	}
}

void ui::render_container(os::Surface* surface, Container& container, float delta_time) {
	if (container.background_color) {
		render::rect_filled(surface, container.rect, *container.background_color);
	}

	for (auto it = container.elements.begin(); it != container.elements.end();) {
		auto& [id, element] = *it;

		bool stale = std::ranges::find(container.current_element_ids, id) == container.current_element_ids.end();
		bool animation_complete = element.animation.update(delta_time, stale);

		if (stale && animation_complete) {
			// animation complete and element stale, remove
			printf("removed %s\n", id.c_str());
			it = container.elements.erase(it);
			continue;
		}

		element.element->render_fn(surface, element.element.get(), element.animation.current);
		++it;
	}
}
