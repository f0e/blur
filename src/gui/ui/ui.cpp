#include "ui.h"

#include "gui/renderer.h"
#include "gui/ui/keys.h"
#include "render.h"
#include "os/draw_text.h"

const int SCROLLBAR_WIDTH = 3;

namespace {
	os::NativeCursor desired_cursor = os::NativeCursor::Arrow;

	ui::AnimatedElement* hovered_element_internal = nullptr;
	ui::AnimatedElement* hovered_element_render = nullptr; // updated at start of each frame

	int get_content_height(const ui::Container& container) {
		int total_height = container.current_position.y - container.get_usable_rect().y;

		// nothing followed the last element, so remove its bottom margin
		total_height -= container.last_margin_bottom;

		return total_height;
	}

	bool can_scroll(const ui::Container& container) {
		int used_space = get_content_height(container);
		return used_space > container.get_usable_rect().h;
	}

	int get_max_scroll(const ui::Container& container) {
		return std::max(get_content_height(container) - container.get_usable_rect().h, 0);
	}

	void render_scrollbar(os::Surface* surface, const ui::Container& container) {
		if (!can_scroll(container))
			return;

		// Calculate total content height
		float total_content_height = get_content_height(container);

		float visible_height = container.get_usable_rect().h;
		float scrollbar_height = (visible_height / total_content_height) * visible_height;

		// Calculate scrollbar vertical position
		float scrollbar_y =
			container.get_usable_rect().y + ((container.scroll_y / total_content_height) * visible_height);

		gfx::Rect scrollbar_rect(
			container.rect.x + container.rect.w - SCROLLBAR_WIDTH, scrollbar_y, SCROLLBAR_WIDTH, scrollbar_height
		);

		render::rounded_rect_filled(surface, scrollbar_rect, gfx::rgba(255, 255, 255, 50), 2.f);
	}
}

void ui::reset_container(
	Container& container,
	const gfx::Rect& rect,
	int line_height,
	const std::optional<Padding>& padding,
	std::optional<gfx::Color> background_color
) {
	container.rect = rect;

	container.current_position = rect.origin();
	container.padding = padding;
	if (container.padding) {
		container.current_position.x += container.padding->left;
		container.current_position.y += container.padding->top;
	}

	container.line_height = line_height;
	container.background_color = background_color;

	container.current_element_ids = {};
	container.updated = false;
	container.last_margin_bottom = 0;
}

ui::Element* ui::add_element(
	Container& container,
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
				_element.rect.y += TYPE_SWITCH_PADDING;
				container.current_position.y += TYPE_SWITCH_PADDING;
			}
		}
	}

	auto* element = add_element(container, std::move(_element), animations);

	// reset x in case it was same line
	container.current_position.x = container.get_usable_rect().x;

	container.current_position.y += element->rect.h + margin_bottom;
	container.last_margin_bottom = margin_bottom;

	return element;
}

ui::Element* ui::add_element(
	Container& container, Element&& _element, const std::unordered_map<size_t, AnimationInitialisation>& animations
) {
	auto& animated_element = container.elements[_element.id];

	_element.orig_rect = _element.rect;

	if (animated_element.element) {
		if (animated_element.element->data != _element.data) {
			container.updated = true;
		}
	}
	else {
		u::log("first added {}", _element.id);

		for (const auto& [animation_id, initialisation] : animations) {
			animated_element.animations.emplace(
				animation_id, AnimationState(initialisation.speed, initialisation.value)
			);
		}
	}

	animated_element.element = std::make_unique<ui::Element>(std::move(_element));

	container.current_element_ids.push_back(animated_element.element->id);

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

	int start_x = container.get_usable_rect().x;
	int shift_y = 0;

	// Calculate the starting y position shift to center elements vertically
	if (vertical) {
		shift_y = (container.get_usable_rect().h - total_height) / 2;
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
				element->rect.x = container.get_usable_rect().center().x - element->rect.w / 2;
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
		int start_group_x = container.get_usable_rect().center().x - (total_width / 2);

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

void ui::set_cursor(os::NativeCursor cursor) {
	desired_cursor = cursor;
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

bool ui::set_hovered_element(AnimatedElement& element) {
	if (hovered_element_internal)
		return false;

	hovered_element_internal = &element;
	return true;
}

ui::AnimatedElement* ui::get_hovered_element() {
	return hovered_element_render;
}

bool ui::update_container_input(Container& container) {
	bool updated = false;

	auto sorted_elements = get_sorted_container_elements(container);

	// update all elements
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

	hovered_element_render = hovered_element_internal;

	// scroll
	if (keys::scroll_delta != 0.f || keys::scroll_delta_precise != 0.f) {
		if (container.rect.contains(keys::mouse_pos)) {
			if (can_scroll(container)) {
				container.scroll_speed_y += keys::scroll_delta;
				keys::scroll_delta = 0.f;

				if (keys::scroll_delta_precise != 0.f) {
					container.scroll_y += keys::scroll_delta_precise;
					keys::scroll_delta_precise = 0.f;

					// immediately clamp to edges todo: overscroll with trackpad?
					int max_scroll = get_max_scroll(container);
					container.scroll_y = std::clamp(container.scroll_y, 0.f, (float)max_scroll);
				}

				updated |=
					true; // if != 0 checks imply that scroll speed changed, no need to explicitly check if it has
			}
		}
	}

	return updated;
}

void ui::on_update_input_start() {
	hovered_element_internal = nullptr;
}

void ui::on_update_input_end() {
	// reset scroll, shouldn't scroll stuff on a later update
	keys::scroll_delta = 0.f;
	keys::scroll_delta_precise = 0.f;

	// set cursor based on if an element wanted pointer
	gui::renderer::set_cursor(desired_cursor);
	desired_cursor = os::NativeCursor::Arrow;
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

void ui::on_update_frame_end() {}

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

void ui::on_frame_start() {}
