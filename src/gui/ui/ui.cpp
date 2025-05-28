#include "ui.h"
#include "keys.h"
#include "../render/render.h"
#include "../sdl.h"

const int SCROLLBAR_WIDTH = 3;
const int SCROLLBAR_GAP = 2;

namespace {
	SDL_SystemCursor desired_cursor = SDL_SYSTEM_CURSOR_DEFAULT;

	ui::AnimatedElement* active_element = nullptr;
	std::string active_element_type;

	ui::AnimatedElement* hovered_element_internal = nullptr;
	std::string hovered_id;

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

	void render_scrollbar(const ui::Container& container) {
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
			container.rect.x + container.rect.w - SCROLLBAR_GAP - SCROLLBAR_WIDTH,
			scrollbar_y,
			SCROLLBAR_WIDTH,
			scrollbar_height
		);

		render::rounded_rect_filled(scrollbar_rect, gfx::Color(255, 255, 255, 50), FLT_MAX);
	}
}

void ui::reset_container(
	Container& container,
	SDL_Window* window,
	const gfx::Rect& rect,
	int element_gap,
	const std::optional<Padding>& padding,
	float line_height,
	std::optional<gfx::Color> background_color
) {
	container.window = window;

	container.rect = rect;

	container.current_position = rect.origin();
	container.padding = padding;
	if (container.padding) {
		container.current_position.x += container.padding->left;
		container.current_position.y += container.padding->top;
	}

	container.element_gap = element_gap;
	container.line_height = line_height;
	container.background_color = background_color;

	container.current_element_ids = {};
	container.updated = false;
	container.last_margin_bottom = 0;
}

ui::AnimatedElement* ui::add_element(
	Container& container,
	Element&& _element,
	int margin_bottom,
	const std::unordered_map<size_t, AnimationState>& animations
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

	auto* animated_element = add_element(container, std::move(_element), animations);

	// reset x in case it was same line
	container.current_position.x = container.get_usable_rect().x;

	container.current_position.y += animated_element->element->rect.h + margin_bottom;
	container.last_margin_bottom = margin_bottom;

	return animated_element;
}

ui::AnimatedElement* ui::add_element(
	Container& container, Element&& _element, const std::unordered_map<size_t, AnimationState>& animations
) {
	auto& animated_element = container.elements[_element.id];

	_element.orig_rect = _element.rect;

	if (animated_element.element) {
		if (animated_element.element->update(_element)) {
			container.updated = true;
		}
		else {
			animated_element.element->rect = _element.rect;
		}
	}
	else {
		u::log("first added {}", _element.id);
		animated_element.element = std::make_unique<ui::Element>(std::move(_element));
		animated_element.animations = animations;
	}

	container.current_element_ids.push_back(animated_element.element->id);

	return &animated_element;
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

// todo: refactor
void ui::center_elements_in_container(Container& container, bool horizontal, bool vertical) {
	int total_height = get_content_height(container);

	int start_x = container.get_usable_rect().x;
	int shift_y = 0;

	// Calculate the starting y position shift to center elements vertically
	if (vertical) {
		shift_y = (container.get_usable_rect().h - total_height) / 2;
		if (shift_y < 0)
			vertical = false;
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
	for (auto& [y_pos, group_elements] : elements_by_y) {
		for (auto& element : group_elements) {
			element->orig_rect = element->rect;
		}
	}
}

void ui::set_cursor(SDL_SystemCursor cursor) {
	desired_cursor = cursor;
}

void ui::set_active_element(AnimatedElement& element, const std::string& type) {
	active_element = &element;
	active_element_type = type;
}

ui::AnimatedElement* ui::get_active_element() {
	return active_element;
}

std::string ui::get_active_element_type() {
	return active_element_type;
}

void ui::reset_active_element() {
	active_element = nullptr;
	active_element_type = "";
}

bool ui::set_hovered_element(AnimatedElement& element) {
	if (hovered_element_internal)
		return false;

	hovered_element_internal = &element;
	return true;
}

std::string ui::get_hovered_id() {
	return hovered_id;
}

bool ui::update_container_input(Container& container) {
	bool updated = false;

	// update all elements
	for (auto& [id, element] : container.elements) {
		bool stale = std::ranges::find(container.current_element_ids, id) == container.current_element_ids.end();
		if (stale)
			continue;

		if (active_element && &element != active_element)
			continue;

		if (element.element->update_fn)
			updated |= (*element.element->update_fn)(container, element);
	}

	hovered_id = hovered_element_internal ? hovered_element_internal->element->id : "";

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

	// empty text events if they werent processed for some reason
	text_event_queue.clear();

	event_queue.clear();

	// set cursor based on if an element wanted pointer
	sdl::set_cursor(desired_cursor);
	desired_cursor = SDL_SYSTEM_CURSOR_DEFAULT;
}

bool ui::update_container_frame(Container& container, float delta_time) {
	bool need_to_render_animation_update = false;

	bool container_stale = std::ranges::all_of(container.elements, [&](const auto& pair) {
		const auto& element = pair.second;
		return std::ranges::find(container.current_element_ids, element.element->id) ==
		       container.current_element_ids.end();
	});

	if (!container_stale) {
		// animate scroll
		float last_scroll_y = container.scroll_y;

		const float scroll_speed_reset_speed = 17.f;
		const float scroll_speed_overscroll_reset_speed = 25.f;
		const float scroll_overscroll_reset_speed = 10.f;
		const float scroll_reset_speed = 10.f;

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
				container.scroll_y =
					u::lerp(container.scroll_y, max_scroll, scroll_overscroll_reset_speed * delta_time);
			}

			if (container.scroll_speed_y != 0.f) {
				container.scroll_y += container.scroll_speed_y * delta_time;
				container.scroll_speed_y =
					u::lerp(container.scroll_speed_y, 0.f, scroll_speed_reset_speed * delta_time);
			}
		}
		else if (container.scroll_y != 0.f) {
			// no longer scrollable but scroll set, reset it
			container.scroll_y = u::lerp(container.scroll_y, 0.f, scroll_reset_speed * delta_time);
		}

		if (container.scroll_y != last_scroll_y)
			need_to_render_animation_update |= true;
	}

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

		if (stale && main_animation.complete) { // animation complete and element stale, remove
			if (element.element->remove_fn)
				(*element.element->remove_fn)(element);

			u::log("removed {}", id);
			it = container.elements.erase(it);
			continue;
		}

		++it;
	}

	return container.updated || need_to_render_animation_update;
}

void ui::on_update_frame_end() {}

void ui::render_container(Container& container) {
	if (container.background_color) {
		render::rect_filled(container.rect, *container.background_color);
	}

	// render::push_clip_rect(container.rect); todo: fade or some shit but straight clipping looks poo

	for (auto& [id, element] : container.elements) {
		element.element->render_fn(container, element);
	}

	if (can_scroll(container)) {
		render_scrollbar(container);
	}

	// render::pop_clip_rect();
}

void ui::on_frame_start() {
	frame++;
}

void ui::on_frame_end() {
	texture_cache::remove_old();
}

void ui::reset_tied_sliders() {
	slider_observers.clear();
}
