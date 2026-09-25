#include "../ui.h"
#include "../helpers/video.h"
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

	render::image(element.element->rect.shrink(IMAGE_INSET), *image_data.texture, tint_color);

	render::borders(
		element.element->rect, gfx::Color(155, 155, 155, stroke_alpha), gfx::Color(80, 80, 80, stroke_alpha)
	);
}

namespace {
	// fits the area the image is drawn in, then puts the border back around it
	gfx::Rect fit_rect(const gfx::Point& position, const gfx::Size& max_size, float aspect_ratio) {
		gfx::Size inner_max_size(
			std::max(max_size.w - (ui::IMAGE_INSET * 2), 1), std::max(max_size.h - (ui::IMAGE_INSET * 2), 1)
		);
		gfx::Rect rect(position, inner_max_size);

		float target_width = rect.h * aspect_ratio;
		float target_height = rect.w / aspect_ratio;

		if (target_width <= rect.w) {
			rect.w = static_cast<int>(std::lround(target_width));
		}
		else {
			rect.h = static_cast<int>(std::lround(target_height));
		}

		rect.w = std::clamp(rect.w, 1, inner_max_size.w);
		rect.h = std::clamp(rect.h, 1, inner_max_size.h);

		rect.w += ui::IMAGE_INSET * 2;
		rect.h += ui::IMAGE_INSET * 2;

		return rect;
	}
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

	// check if we already have this element
	if (container.elements.contains(id)) {
		auto& image_data = std::get<ImageElementData>(container.elements[id].element->data);
		if (image_data.image_id == image_id)
			texture = image_data.texture;
	}

	if (!texture) {
		texture = texture_cache::get_or_load_texture(image_path, image_id);

		if (!texture) {
			u::log("{} failed to load image (id: {})", id, image_id);

			// fall back to last texture if available
			if (container.elements.contains(id)) {
				auto& image_data = std::get<ImageElementData>(container.elements[id].element->data);
				texture = image_data.texture;
			}

			if (!texture)
				return {};
		}
		else {
			u::log("{} loaded image (id: {})", id, image_id);
		}
	}

	return add_image(id, container, texture, max_size, image_id, image_color);
}

std::optional<ui::AnimatedElement*> ui::add_image(
	const std::string& id,
	Container& container,
	std::shared_ptr<render::Texture> texture,
	const gfx::Size& max_size,
	const std::string& image_id,
	gfx::Color image_color
) {
	if (!texture || !texture->is_valid())
		return {};

	// check if we already have this element with the same id — reuse if so
	if (container.elements.contains(id)) {
		Element& cached_element = *container.elements[id].element;
		auto& image_data = std::get<ImageElementData>(cached_element.data);

		if (image_data.image_id == image_id)
			texture = image_data.texture;
	}

	float aspect_ratio = texture->width() / static_cast<float>(texture->height());
	gfx::Rect image_rect = fit_rect(container.current_position, max_size, aspect_ratio);

	Element element(
		id,
		ElementType::IMAGE,
		image_rect,
		ImageElementData{
			.texture = texture,
			.image_id = image_id,
			.image_color = image_color,
		},
		render_image
	);

	return add_element(container, std::move(element), container.element_gap);
}

void ui::render_video_frame(const Container& container, const AnimatedElement& element) {
	const auto& data = std::get<VideoFrameElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	int stroke_alpha = anim * 125;

	data.player->draw(element.element->rect.shrink(IMAGE_INSET), data.color.adjust_alpha(anim));

	render::borders(
		element.element->rect, gfx::Color(155, 155, 155, stroke_alpha), gfx::Color(80, 80, 80, stroke_alpha)
	);
}

std::optional<ui::AnimatedElement*> ui::add_video_frame(
	const std::string& id,
	Container& container,
	std::shared_ptr<VideoPlayer> player,
	const gfx::Size& max_size,
	gfx::Color color
) {
	if (!player || !player->is_video_ready())
		return {};

	auto dimensions = player->get_video_dimensions();
	if (!dimensions)
		return {};

	float aspect_ratio = dimensions->first / static_cast<float>(dimensions->second);

	Element element(
		id,
		ElementType::VIDEO_FRAME,
		fit_rect(container.current_position, max_size, aspect_ratio),
		VideoFrameElementData{
			.player = std::move(player),
			.color = color,
		},
		render_video_frame
	);

	return add_element(container, std::move(element), container.element_gap);
}
