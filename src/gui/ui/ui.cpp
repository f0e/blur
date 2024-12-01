#include "ui.h"

#include <algorithm>
#include <utility>
#include "gui/ui/keys.h"
#include "render.h"
#include "os/draw_text.h"

namespace {
	int get_content_height(const ui::Container& container) {
		int total_height = container.current_position.y - container.rect.y;

		// nothing followed the last element, so remove its bottom margin
		total_height -= container.last_margin_bottom;

		return total_height;
	}

	bool can_scroll(const ui::Container& container) {
		int used_space = get_content_height(container);
		return used_space > container.rect.h;
	}

	int get_max_scroll(const ui::Container& container) {
		return std::max(get_content_height(container) - container.rect.h, 0);
	}

	void render_scrollbar(os::Surface* surface, const ui::Container& container) {
		if (!can_scroll(container))
			return;

		// Calculate total content height
		float total_content_height = get_content_height(container);

		// Calculate visible area height
		float visible_height = container.rect.h;

		// Calculate scrollbar height proportional to visible vs total content
		float scrollbar_height = (visible_height / total_content_height) * visible_height;

		// Calculate scrollbar vertical position
		float scrollbar_y = container.rect.y + ((container.scroll_y / total_content_height) * visible_height);

		// Create scrollbar rectangle
		gfx::Rect scrollbar_rect(
			container.rect.x + container.rect.w - 8, // Position near right edge
			scrollbar_y,
			3, // Thin width
			scrollbar_height
		);

		// Render scrollbar with slight transparency
		render::rounded_rect_filled(
			surface,
			scrollbar_rect,
			gfx::rgba(255, 255, 255, 50),
			2.f // Slight rounding
		);
	}
}

void ui::reset_container(
	Container& container, const gfx::Rect& rect, int line_height, std::optional<gfx::Color> background_color
) {
	container.line_height = line_height;
	container.rect = rect;
	container.background_color = background_color;
	container.current_position = rect.origin(); // todo: padding
	container.current_element_ids = {};
	container.updated = false;
	container.last_margin_bottom = 0;
}

ui::Element* ui::add_element(
	Container& container,
	const std::string& id,
	Element&& _element,
	int margin_bottom,
	const std::unordered_map<size_t, AnimationInitialisation>& animations
) {
	// pad when switching element type
	if (container.current_element_ids.size() > 0) {
		auto& last_element_id = container.current_element_ids.back();
		auto& last_element = container.elements[last_element_id];

		static std::set ignore_types = { ElementType::SEPARATOR };

		if (!ignore_types.contains(last_element.element->type) && !ignore_types.contains(_element.type)) {
			if (_element.type != last_element.element->type) {
				_element.rect.y += type_switch_padding;
				container.current_position.y += type_switch_padding;
			}
		}
	}

	auto* element = add_element(container, id, std::move(_element), animations);

	container.current_position.x = container.rect.x; // reset x in case it was same line

	container.current_position.y += element->rect.h + margin_bottom;
	container.last_margin_bottom = margin_bottom;

	return element;
}

ui::Element* ui::add_element(
	Container& container,
	const std::string& id,
	Element&& _element,
	const std::unordered_map<size_t, AnimationInitialisation>& animations
) {
	auto& animated_element = container.elements[id];

	_element.orig_rect = _element.rect;

	if (animated_element.element) {
		if (animated_element.element->data != _element.data) {
			container.updated = true;
		}
	}
	else {
		u::log("first added {}", id);

		for (const auto& [animation_id, initialisation] : animations) {
			animated_element.animations.emplace(
				animation_id, AnimationState(initialisation.speed, initialisation.value)
			);
		}
	}

	animated_element.element = std::make_unique<ui::Element>(std::move(_element));

	container.current_element_ids.push_back(id);

	return animated_element.element.get();
}

void ui::add_spacing(Container& container, int spacing) {
	container.current_position.y += spacing;
}

void ui::set_next_same_line(Container& container) {
	if (container.current_element_ids.empty())
		return;

	const std::string& last_element_id = container.current_element_ids.back();
	auto& last_element = container.elements[last_element_id].element;

	container.current_position.x = last_element->rect.x2() + container.last_margin_bottom;
	container.current_position.y = last_element->rect.y;
}

void ui::center_elements_in_container(Container& container, bool horizontal, bool vertical) {
	int total_height = get_content_height(container);

	int start_x = container.rect.x;
	int shift_y = 0;

	// Calculate the starting y position shift to center elements vertically
	if (vertical) {
		shift_y = (container.rect.h - total_height) / 2;
	}

	// Group elements by their y position
	std::map<int, std::vector<Element*>> elements_by_y;
	for (const auto& id : container.current_element_ids) {
		auto& element = container.elements[id].element;

		if (element->fixed)
			continue;

		elements_by_y[element->rect.y].push_back(element.get());
	}

	// Update element positions
	for (auto& [y_pos, group_elements] : elements_by_y) {
		// If only one element in the group, center it directly
		if (group_elements.size() == 1) {
			auto& element = group_elements[0];

			// Adjust y position by the calculated shift without overwriting it
			if (vertical) {
				element->rect.y += shift_y;
			}

			// Center horizontally if requested
			if (horizontal) {
				element->rect.x = container.rect.center().x - element->rect.w / 2;
			}
			continue;
		}

		// Calculate total width and spacing of group
		int total_width = 0;
		std::vector<int> x_offsets;
		x_offsets.push_back(0); // First element starts at 0 offset

		for (size_t i = 1; i < group_elements.size(); ++i) {
			int spacing = group_elements[i]->rect.x - (group_elements[i - 1]->rect.x + group_elements[i - 1]->rect.w);
			x_offsets.push_back(x_offsets.back() + group_elements[i - 1]->rect.w + spacing);
			total_width = x_offsets.back() + group_elements.back()->rect.w;
		}

		// Calculate starting x to center the entire group
		int start_group_x = container.rect.center().x - (total_width / 2);

		// Reposition elements
		for (size_t i = 0; i < group_elements.size(); ++i) {
			auto& element = group_elements[i];

			// Adjust y position by the calculated shift without overwriting it
			if (vertical) {
				element->rect.y += shift_y;
			}

			// Horizontally center the group while preserving relative spacing
			if (horizontal) {
				element->rect.x = start_group_x + x_offsets[i];
			}
		}
	}

	// update original rects for scrolling
	for (auto& [id, element] : container.elements)
		element.element->orig_rect = element.element->rect;
}

std::vector<decltype(ui::Container::elements)::iterator> ui::get_sorted_container_elements(Container& container) {
	std::vector<decltype(container.elements)::iterator> sorted_elements;
	sorted_elements.reserve(container.elements.size());

	for (auto it = container.elements.begin(); it != container.elements.end(); ++it) {
		sorted_elements.push_back(it);
	}

	std::ranges::stable_sort(sorted_elements, [](const auto& lhs, const auto& rhs) {
		return lhs->second.z_index > rhs->second.z_index;
	});

	return sorted_elements;
}

bool ui::update_container_input(Container& container) {
	bool updated = false;

	auto sorted_elements = get_sorted_container_elements(container);

	for (auto& it : sorted_elements) {
		const auto& id = it->first;
		auto& element = it->second;

		bool stale = std::ranges::find(container.current_element_ids, id) == container.current_element_ids.end();
		if (stale)
			continue;

		if (active_element && &element != active_element)
			continue;

		if (element.element->update_fn)
			updated |= (*element.element->update_fn)(container, element);
	}

	if (keys::scroll_delta != 0.f || keys::scroll_delta_precise != 0.f) {
		if (container.rect.contains(keys::mouse_pos)) {
			if (can_scroll(container)) {
				float last_scroll_speed_y = container.scroll_speed_y;

				container.scroll_speed_y += keys::scroll_delta;
				keys::scroll_delta = 0.f;

				if (keys::scroll_delta_precise != 0.f) {
					container.scroll_y += keys::scroll_delta_precise;
					keys::scroll_delta_precise = 0.f;

					// immediately clamp to edges todo: overscroll with trackpad?
					int max_scroll = get_max_scroll(container);
					container.scroll_y = std::clamp(container.scroll_y, 0.f, (float)max_scroll);
				}

				updated = container.scroll_speed_y != last_scroll_speed_y;
			}
		}
	}

	return updated;
}

void ui::on_update_end() {
	keys::scroll_delta = 0.f;
	keys::scroll_delta_precise = 0.f;
}

bool ui::update_container_frame(Container& container, float delta_time) {
	bool need_to_render_animation_update = false;

	// animate scroll
	float last_scroll_y = container.scroll_y;

	const int scroll_speed_reset_speed = 17.f;
	const int scroll_speed_overscroll_reset_speed = 25.f;
	const int scroll_overscroll_reset_speed = 10.f;

	if (can_scroll(container)) {
		// clamp scroll
		int max_scroll = get_max_scroll(container);
		if (container.scroll_y < 0) {
			container.scroll_speed_y =
				u::lerp(container.scroll_speed_y, 0.f, scroll_speed_overscroll_reset_speed * delta_time);
			container.scroll_y = u::lerp(container.scroll_y, 0.f, scroll_overscroll_reset_speed * delta_time);
		}
		else if (container.scroll_y > max_scroll) {
			container.scroll_speed_y =
				u::lerp(container.scroll_speed_y, 0.f, scroll_speed_overscroll_reset_speed * delta_time);
			container.scroll_y = u::lerp(container.scroll_y, max_scroll, scroll_overscroll_reset_speed * delta_time);
		}

		if (container.scroll_speed_y != 0.f) {
			container.scroll_y += container.scroll_speed_y * delta_time;
			container.scroll_speed_y = u::lerp(container.scroll_speed_y, 0.f, scroll_speed_reset_speed * delta_time);
		}
	}
	else if (container.scroll_y != 0.f) {
		// no longer scrollable but scroll set, reset it
		container.scroll_y = 0.f;
	}

	if (container.scroll_y != last_scroll_y)
		need_to_render_animation_update |= true;

	// update elements
	for (auto it = container.elements.begin(); it != container.elements.end();) {
		auto& [id, element] = *it;

		// hacky, idc.
		element.element->rect.y = element.element->orig_rect.y - container.scroll_y;

		auto& main_animation = element.animations.at(hasher("main"));

		bool stale = std::ranges::find(container.current_element_ids, id) == container.current_element_ids.end();
		main_animation.set_goal(!stale ? 1.f : 0.f);

		for (auto& [animation_id, animation] : element.animations) {
			need_to_render_animation_update |= animation.update(delta_time);
		}

		if (stale && main_animation.complete) {
			// animation complete and element stale, remove
			u::log("removed {}", id);
			it = container.elements.erase(it);
			continue;
		}

		++it;
	}

	return container.updated || need_to_render_animation_update;
}

void ui::render_container(os::Surface* surface, Container& container) {
	if (container.background_color) {
		render::rect_filled(surface, container.rect, *container.background_color);
	}

	// render::push_clip_rect(surface, container.rect); todo: fade or some shit but straight clipping looks poo

	auto sorted_elements = get_sorted_container_elements(container);

	for (auto& it : std::ranges::reverse_view(sorted_elements)) {
		const auto& id = it->first;
		auto& element = it->second;

		element.element->render_fn(container, surface, element);
	}

	if (can_scroll(container)) {
		render_scrollbar(surface, container);
	}

	// render::pop_clip_rect(surface);
}
