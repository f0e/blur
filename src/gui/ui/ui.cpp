#include "ui.h"
#include "render.h"
#include "os/draw_text.h"

void ui::reset_container(
	Container& container, const gfx::Rect& rect, const SkFont& font, std::optional<gfx::Color> background_color
) {
	container.line_height = font.getSize();
	container.rect = rect;
	container.background_color = background_color;
	container.current_position = rect.origin(); // todo: padding
	container.current_element_ids = {};
	container.updated = false;
	container.last_margin_bottom = 0;
}

ui::Element* ui::add_element(Container& container, const std::string& id, Element&& _element, int margin_bottom) {
	auto* element = add_element(container, id, std::move(_element));

	container.current_position.y += element->rect.h + margin_bottom;
	container.last_margin_bottom = margin_bottom;

	return element;
}

ui::Element* ui::add_element(Container& container, const std::string& id, Element&& _element) {
	auto& container_element = container.elements[id];

	if (container_element.element) {
		if (container_element.element->data != _element.data) {
			container.updated = true;
		}
	}
	else {
		u::log("first added {}", id);
	}

	container_element.element = std::make_unique<ui::Element>(std::move(_element));

	container.current_element_ids.push_back(id);

	return container_element.element.get();
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

bool ui::update_container(Container& container, float delta_time) {
	bool all_animations_complete = true;

	for (auto it = container.elements.begin(); it != container.elements.end();) {
		auto& [id, element] = *it;

		bool stale = std::ranges::find(container.current_element_ids, id) == container.current_element_ids.end();
		element.animation.update(delta_time, stale);

		if (stale && element.animation.complete) {
			// animation complete and element stale, remove
			u::log("removed {}", id);
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
