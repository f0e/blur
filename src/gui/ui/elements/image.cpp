#include "../ui.h"
#include "../../render/render.h"

struct ImageElementData {
	std::filesystem::path image_path;
	std::shared_ptr<render::Texture> texture;
	std::string image_id;
	gfx::Color image_color;
};

void ui::render_image(const Container& container, const AnimatedElement& element) {
	const auto& image_data = std::get<ImageElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	int alpha = anim * 255;
	int stroke_alpha = anim * 125;

	gfx::Color tint_color = image_data.image_color.adjust_alpha(anim);

	render::image(element.element->rect.shrink(3), *image_data.texture, tint_color);

	render::borders(
		element.element->rect, gfx::Color(155, 155, 155, stroke_alpha), gfx::Color(80, 80, 80, stroke_alpha)
	);
}

std::optional<ui::AnimatedElement*> ui::add_image(
	const std::string& id,
	Container& container,
	const std::filesystem::path& image_path,
	const gfx::Size& max_size,
	std::string image_id,
	gfx::Color image_color
) {
	std::shared_ptr<render::Texture> texture;
	std::shared_ptr<render::Texture> last_texture;

	// Check if we already have this element
	if (container.elements.contains(id)) {
		Element& cached_element = *container.elements[id].element;
		auto& image_data = std::get<ImageElementData>(cached_element.data);

		if (image_data.image_id == image_id) {
			// Reuse the existing texture if ID matches
			texture = image_data.texture;
		}
		else {
			// Keep the last texture as fallback
			last_texture = image_data.texture;
		}
	}

	// Load image if new
	if (!texture) {
		texture = texture_cache::get_or_load_texture(image_path, image_id);

		if (!texture) {
			u::log("{} failed to load image (id: {})", id, image_id);
			if (last_texture) {
				// Use last image as fallback
				texture = last_texture;
			}
			else {
				return {};
			}
		}

		u::log("{} loaded image (id: {})", id, image_id);
	}

	gfx::Rect image_rect(container.current_position, max_size);

	// Calculate aspect ratio and adjust dimensions
	float aspect_ratio = texture->width() / static_cast<float>(texture->height());

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

	Element element(
		id,
		ElementType::IMAGE,
		image_rect,
		ImageElementData{
			.image_path = image_path,
			.texture = texture,
			.image_id = image_id,
			.image_color = image_color,
		},
		render_image
	);

	return add_element(container, std::move(element), container.element_gap);
}
