#include "ui.h"
#include "os/draw_text.h"
#include "os/sampling.h"
#include "render.h"

gfx::Color adjust_color(const gfx::Color& color, float anim) {
	return gfx::rgba(gfx::getr(color), gfx::getg(color), gfx::getb(color), round(gfx::geta(color) * anim)); // seta is broken or smth i swear
}

bool has_content_changed(const ui::Element* existing, const ui::Element* new_element) {
	if (existing->type != new_element->type)
		return true;

	switch (existing->type) {
		case ui::ElementType::BAR: {
			const auto& existing_data = std::get<ui::BarElementData>(existing->data);
			const auto& new_data = std::get<ui::BarElementData>(new_element->data);
			return existing_data.percent_fill != new_data.percent_fill ||
			       existing_data.background_color != new_data.background_color ||
			       existing_data.fill_color != new_data.fill_color ||
			       existing_data.bar_text != new_data.bar_text ||
			       existing_data.text_color != new_data.text_color;
		}
		case ui::ElementType::TEXT: {
			const auto& existing_data = std::get<ui::TextElementData>(existing->data);
			const auto& new_data = std::get<ui::TextElementData>(new_element->data);
			return existing_data.text != new_data.text ||
			       existing_data.color != new_data.color ||
			       existing_data.align != new_data.align;
		}
		case ui::ElementType::IMAGE: {
			const auto& existing_data = std::get<ui::ImageElementData>(existing->data);
			const auto& new_data = std::get<ui::ImageElementData>(new_element->data);
			return existing_data.image_id != new_data.image_id ||
			       existing_data.image_path != new_data.image_path;
		}
		default:
			return false;
	}
}

void ui::render_bar(os::Surface* surface, const Element* element, float anim) {
	const int text_gap = 7;

	auto& bar_data = std::get<BarElementData>(element->data);

	gfx::Color adjusted_background_color = adjust_color(bar_data.background_color, anim);
	gfx::Color adjusted_fill_color = adjust_color(bar_data.fill_color, anim);

	gfx::Size text_size(0, 0);

	if (bar_data.bar_text && bar_data.text_color && bar_data.font) {
		gfx::Color adjusted_text_color = adjust_color(*bar_data.text_color, anim);

		gfx::Point text_pos = element->rect.origin();

		text_size = render::get_text_size(*bar_data.bar_text, **bar_data.font);

		text_pos.x = element->rect.x2();
		text_pos.y += (*bar_data.font)->getSize() / 2;

		render::text(surface, text_pos, adjusted_text_color, *bar_data.bar_text, **bar_data.font, os::TextAlign::Right);
	}

	gfx::Rect bar_rect = element->rect;
	bar_rect.w -= text_size.w + text_gap;

	render::rounded_rect_filled(surface, bar_rect, adjusted_background_color, 1000.f);

	if (bar_data.percent_fill > 0) {
		gfx::Rect fill_rect = bar_rect;
		fill_rect.w = static_cast<int>(bar_rect.w * bar_data.percent_fill);
		render::rounded_rect_filled(surface, fill_rect, adjusted_fill_color, 1000.f);
	}
}

void ui::render_text(os::Surface* surface, const Element* element, float anim) {
	auto& text_data = std::get<TextElementData>(element->data);

	gfx::Color adjusted_color = adjust_color(text_data.color, anim);

	gfx::Point text_pos = element->rect.origin();
	text_pos.y += text_data.font.getSize() - 1;

	render::text(surface, text_pos, adjusted_color, text_data.text, text_data.font, text_data.align);
}

void ui::render_image(os::Surface* surface, const Element* element, float anim) {
	auto& image_data = std::get<ImageElementData>(element->data);

	int alpha = anim * 255;
	int stroke_alpha = anim * 125;

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

void ui::init_container(Container& container, gfx::Rect rect, const SkFont& font, std::optional<gfx::Color> background_color) {
	container.line_height = font.getSize();
	container.rect = rect;
	container.background_color = background_color;
	container.current_position = rect.origin(); // todo: padding
	container.current_element_ids = {};
	container.updated = false;
	container.last_margin_bottom = 0;
}

void ui::add_element(Container& container, const std::string& id, std::shared_ptr<Element> element, int margin_bottom) {
	container.current_position.y += element->rect.h + margin_bottom;
	container.last_margin_bottom = margin_bottom;

	add_element_fixed(container, id, element);
}

void ui::add_element_fixed(Container& container, const std::string& id, std::shared_ptr<Element> element) {
	if (!container.elements.contains(id)) {
		container.elements[id] = {
			element,
		};
		printf("first added %s\n", id.c_str());
	}
	else {
		auto& container_element = container.elements[id];
		if (container_element.element->data != element->data) {
			container.updated = true;
		}
		container_element.element = element;
	}

	container.current_element_ids.push_back(id);
}

std::shared_ptr<ui::Element> ui::add_bar(const std::string& id, Container& container, float percent_fill, gfx::Color background_color, gfx::Color fill_color, int bar_width, std::optional<std::string> bar_text, std::optional<gfx::Color> text_color, std::optional<const SkFont*> font) {
	auto element = std::make_shared<Element>(Element{
		ElementType::BAR,
		gfx::Rect(container.current_position, gfx::Size(bar_width, 6)),
		BarElementData{ percent_fill, background_color, fill_color, bar_text, text_color, font },
		render_bar,
	});

	add_element(container, id, element, container.line_height);

	return element;
}

std::shared_ptr<ui::Element> ui::add_text(const std::string& id, Container& container, const std::string& text, gfx::Color color, const SkFont& font, os::TextAlign align, int margin_bottom) {
	auto element = std::make_shared<Element>(Element{
		ElementType::TEXT,
		gfx::Rect(container.current_position, gfx::Size(0, font.getSize())),
		TextElementData{ text, color, font, align },
		render_text,
	});

	add_element(container, id, element, margin_bottom);

	return element;
}

std::shared_ptr<ui::Element> ui::add_text_fixed(const std::string& id, Container& container, gfx::Point position, const std::string& text, gfx::Color color, const SkFont& font, os::TextAlign align, int margin_bottom) {
	auto element = std::make_shared<Element>(Element{
		ElementType::TEXT,
		gfx::Rect(position, gfx::Size(0, font.getSize())),
		TextElementData{ text, color, font, align },
		render_text,
		true,
	});

	add_element_fixed(container, id, element);

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

	auto element = std::make_shared<Element>(Element{
		ElementType::IMAGE,
		image_rect,
		ImageElementData{ image_path, image_surface, image_id },
		render_image,
	});

	add_element(container, id, element, container.line_height);

	return element;
}

void ui::center_elements_in_container(Container& container, bool horizontal, bool vertical) {
	int total_height = container.current_position.y - container.rect.y;

	// nothing followed the last element, so remove its bottom margin
	total_height -= container.last_margin_bottom;

	int start_x = container.rect.x;
	int shift_y = 0;

	// Calculate the starting y position shift to center elements vertically
	if (vertical) {
		shift_y = (container.rect.h - total_height) / 2;
	}

	// Calculate the starting x position to center elements horizontally
	if (horizontal) {
		int total_width = 0;
		for (const auto& id : container.current_element_ids) {
			auto& element = container.elements[id].element;

			if (element->fixed)
				continue;

			total_width = std::max(total_width, element->rect.w);
		}
		start_x = (container.rect.w - total_width) / 2 + container.rect.x;
	}

	// Update element positions
	for (const auto& id : container.current_element_ids) {
		auto& element = container.elements[id].element;

		if (element->fixed)
			continue;

		// Adjust y position by the calculated shift without overwriting it
		if (vertical) {
			element->rect.y += shift_y;
		}

		// Center x position if horizontal centering is enabled
		if (horizontal) {
			element->rect.x = container.rect.center().x - element->rect.w / 2;
		}
	}
}

bool ui::update_container(os::Surface* surface, Container& container, float delta_time) {
	bool all_animations_complete = true;

	for (auto it = container.elements.begin(); it != container.elements.end();) {
		auto& [id, element] = *it;

		bool stale = std::ranges::find(container.current_element_ids, id) == container.current_element_ids.end();
		element.animation.update(delta_time, stale);

		if (stale && element.animation.complete) {
			// animation complete and element stale, remove
			printf("removed %s\n", id.c_str());
			it = container.elements.erase(it);
			continue;
		}

		if (!element.animation.complete || !element.animation.rendered_complete)
			all_animations_complete = false;

		++it;
	}

	return container.updated || !all_animations_complete;
}

void ui::render_container(os::Surface* surface, Container& container) {
	if (container.background_color) {
		render::rect_filled(surface, container.rect, *container.background_color);
	}

	for (auto& [id, element] : container.elements) {
		element.element->render_fn(surface, element.element.get(), element.animation.current);

		if (element.animation.complete) {
			element.animation.rendered_complete = true;
		}
	}
}
