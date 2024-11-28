#include "../ui.h"

#include "gui/ui/utils.h"

void ui::render_image(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	static const float rounding = 7.8f;

	const auto& image_data = std::get<ImageElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	int alpha = anim * 255;
	int stroke_alpha = anim * 125;

	gfx::Rect image_rect = element.element->rect;
	image_rect.shrink(3);

	os::Paint paint;
	paint.color(utils::adjust_color(image_data.image_color, anim));
	surface->drawSurface(
		image_data.image_surface.get(), image_data.image_surface->bounds(), image_rect, os::Sampling(), &paint
	);

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

std::optional<ui::Element*> ui::add_image(
	const std::string& id,
	Container& container,
	const std::filesystem::path& image_path,
	const gfx::Size& max_size,
	std::string image_id,
	gfx::Color image_color
) {
	os::SurfaceRef image_surface;
	os::SurfaceRef last_image_surface;

	// get existing image
	if (container.elements.contains(id)) {
		Element& cached_element = *container.elements[id].element;
		auto& image_data = std::get<ImageElementData>(cached_element.data);
		if (image_data.image_id == image_id) { // edge cases this might not work, it's using current_frame, maybe image
			                                   // gets written after ffmpeg reports progress? idk. good enough for now
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
			u::log("{} failed to load image (id: {})", id, image_id);
			if (last_image_surface) {
				// use last image as fallback todo: this is a bit hacky make it better
				image_surface = last_image_surface;
			}
			else {
				return {};
			}
		}

		u::log("{} loaded image (id: {})", id, image_id);
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

	Element element = {
		.type = ElementType::IMAGE,
		.rect = image_rect,
		.data =
			ImageElementData{
				.image_path = image_path,
				.image_surface = image_surface,
				.image_id = image_id,
				.image_color = image_color,
			},
		.render_fn = render_image,
	};

	return add_element(container, id, std::move(element), container.line_height);
}
