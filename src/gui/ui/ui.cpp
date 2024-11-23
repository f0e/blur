#include "ui.h"
#include "os/draw_text.h"
#include "os/sampling.h"
#include "render.h"

const int gap = 15;

void ui::render_bar(os::Surface* surface, const Element* element, float anim) {
	const int text_gap = 7;

	auto& bar_data = std::get<BarElementData>(element->data);

	gfx::seta(bar_data.background_color, anim * gfx::geta(bar_data.background_color));
	gfx::seta(bar_data.fill_color, anim * gfx::geta(bar_data.fill_color));

	gfx::Size text_size(0, 0);

	if (bar_data.bar_text && bar_data.text_color && bar_data.font) {
		gfx::seta(*bar_data.text_color, anim * gfx::geta(*bar_data.text_color));

		gfx::Point text_pos = element->rect.origin();

		text_size = render::get_text_size(*bar_data.bar_text, **bar_data.font);

		text_pos.x = element->rect.x2();
		text_pos.y += (*bar_data.font)->getSize() / 2;

		render::text(surface, text_pos, *bar_data.text_color, *bar_data.bar_text, **bar_data.font, os::TextAlign::Right);
	}

	gfx::Rect bar_rect = element->rect;
	bar_rect.w -= text_size.w + text_gap;

	render::rounded_rect_filled(surface, bar_rect, bar_data.background_color, 1000.f);

	if (bar_data.percent_fill > 0) {
		gfx::Rect fill_rect = bar_rect;
		fill_rect.w = static_cast<int>(bar_rect.w * bar_data.percent_fill);
		render::rounded_rect_filled(surface, fill_rect, bar_data.fill_color, 1000.f);
	}
}

void ui::render_text(os::Surface* surface, const Element* element, float anim) {
	auto& text_data = std::get<TextElementData>(element->data);

	gfx::seta(text_data.color, anim * gfx::geta(text_data.color));
	if (gfx::geta(text_data.color) == 0)
		return;

	gfx::Point text_pos = element->rect.origin();
	text_pos.y += text_data.font.getSize() - 1;

	render::text(surface, text_pos, text_data.color, text_data.text, text_data.font, text_data.align);
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

void ui::add_element(Container& container, const std::string& id, std::shared_ptr<Element> element) {
	container.current_position.y += element->rect.h + gap;

	if (!container.elements.contains(id)) {
		container.elements[id] = {
			element,
		};
		printf("first added %s\n", id.c_str());
	}
	else {
		auto& container_element = container.elements[id];
		container_element.element = element;
	}

	container.current_element_ids.push_back(id);
}

std::shared_ptr<ui::Element> ui::add_bar(const std::string& id, Container& container, float percent_fill, gfx::Color background_color, gfx::Color fill_color, int bar_width, std::optional<std::string> bar_text, std::optional<gfx::Color> text_color, std::optional<const SkFont*> font) {
	auto element = std::make_shared<Element>(
		ElementType::BAR,
		gfx::Rect(container.current_position, gfx::Size(bar_width, 6)),
		BarElementData{ percent_fill, background_color, fill_color, bar_text, text_color, font },
		render_bar
	);

	add_element(container, id, element);

	return element;
}

std::shared_ptr<ui::Element> ui::add_text(const std::string& id, Container& container, const std::string& text, gfx::Color color, const SkFont& font, os::TextAlign align) {
	auto element = std::make_shared<Element>(
		ElementType::TEXT,
		gfx::Rect(container.current_position, gfx::Size(0, font.getSize())),
		TextElementData{ text, color, font, align },
		render_text
	);

	add_element(container, id, element);

	return element;
}

std::optional<std::shared_ptr<ui::Element>> ui::add_image(const std::string& id, Container& container, std::string image_path, gfx::Size max_size, std::string image_id) {
	os::SurfaceRef image_surface;
	os::SurfaceRef last_image_surface;

	// get existing image
	if (container.elements.contains(id)) {
		std::shared_ptr<Element> cached_element = container.elements[id].element;
		auto& image_data = std::get<ImageElementData>(cached_element->data);
		if (image_data.image_id == image_id) { // edge cases this might not work, it's using current_frame, maybe image gets written after ffmpeg reports progress? idk. good enough for now
			image_surface = image_data.image_surface;
		}
		else {
			last_image_surface = image_data.image_surface;
		}
	}

	// load image if new
	if (!image_surface) {
		image_surface = os::instance()->loadRgbaSurface(image_path.c_str());

		if (!image_surface) {
			printf("%s failed to load image (id: %s)\n", id.c_str(), image_id.c_str());
			if (last_image_surface) {
				// use last image as fallback todo: this is a bit hacky make it better
				image_surface = last_image_surface;
			}
			else {
				return {};
			}
		}

		printf("%s loaded image (id: %s)\n", id.c_str(), image_id.c_str());
	}

	gfx::Rect image_rect(container.current_position, max_size);

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

	auto element = std::make_shared<Element>(
		ElementType::IMAGE,
		image_rect,
		ImageElementData{ image_path, image_surface, image_id },
		render_image
	);

	add_element(container, id, element);

	return element;
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
