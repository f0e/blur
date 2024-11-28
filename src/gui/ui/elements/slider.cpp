#include "../ui.h"
#include "../render.h"
#include "../utils.h"
#include "../keys.h"

#include "../../renderer.h"

const float slider_rounding = 4.0f;
const float handle_width = 10.0f;
const int track_height = 5;
const int line_height_add = 5;

void ui::render_slider(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	const auto& slider_data = std::get<SliderElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;

	int line_height = slider_data.font.getSize();

	// Determine current value and range based on type
	float min_val = std::visit(
		[](auto&& arg) {
			return static_cast<float>(arg);
		},
		slider_data.min_value
	);
	float max_val = std::visit(
		[](auto&& arg) {
			return static_cast<float>(arg);
		},
		slider_data.max_value
	);
	float current_val = std::visit(
		[](auto&& arg) {
			// Dereference the pointer and cast the value to float
			return static_cast<float>(*arg);
		},
		slider_data.current_value
	);

	// Normalize progress
	float progress = (current_val - min_val) / (max_val - min_val);
	float clamped_progress = std::clamp(progress, 0.f, 1.f);

	int track_shade = 40 + (20 * hover_anim);
	gfx::Color track_color = utils::adjust_color(gfx::rgba(track_shade, track_shade, track_shade, 255), anim);

	int fill_shade = 55 + (55 * hover_anim);
	gfx::Color filled_color = utils::adjust_color(gfx::rgba(fill_shade, fill_shade, fill_shade, 255), anim);
	gfx::Color handle_border_color = utils::adjust_color(gfx::rgba(0, 0, 0, 50), anim);

	gfx::Color text_color = utils::adjust_color(gfx::rgba(255, 255, 255, 255), anim);
	gfx::Color tooltip_color = utils::adjust_color(gfx::rgba(125, 125, 125, 255), anim);

	// render::rect_filled(surface, element.element->rect, gfx::rgba(255, 0, 0, 100));

	gfx::Rect track_rect = element.element->rect;
	track_rect.shrink(handle_width / 2);
	track_rect.y = track_rect.y2() - handle_width / 2;
	track_rect.h = track_height;

	// Draw track
	render::rounded_rect_filled(surface, track_rect, track_color, slider_rounding);

	// Draw filled portion
	gfx::Rect filled_rect = track_rect;
	filled_rect.w *= clamped_progress;
	render::rounded_rect_filled(surface, filled_rect, filled_color, slider_rounding);

	// Calculate handle position
	if (progress >= 0.f && progress <= 1.f) {
		float handle_x = track_rect.x + (track_rect.w * clamped_progress) - (handle_width / 2);
		gfx::Rect handle_rect(handle_x, track_rect.y - ((handle_width - track_height) / 2), handle_width, handle_width);

		// Draw handle
		auto tmp = handle_rect;
		tmp.enlarge(1);
		render::rounded_rect_filled(surface, tmp, handle_border_color, handle_width);
		render::rounded_rect_filled(surface, handle_rect, filled_color, handle_width);
	}

	std::string label;
	std::visit(
		[&](auto&& arg) {
			label = std::vformat(slider_data.label_format, std::make_format_args(*arg));
		},
		slider_data.current_value
	);

	gfx::Point label_pos = element.element->rect.origin();
	label_pos.y += slider_data.font.getSize();

	render::text(surface, label_pos, text_color, label, slider_data.font, os::TextAlign::Left);

	label_pos.y += line_height_add;
	label_pos.y += slider_data.font.getSize();

	render::text(surface, label_pos, tooltip_color, slider_data.tooltip, slider_data.font, os::TextAlign::Left);
}

bool ui::update_slider(const Container& container, AnimatedElement& element) {
	auto& slider_data = std::get<SliderElementData>(element.element->data);
	auto& hover_anim = element.animations.at(hasher("hover"));

	gfx::Rect track_rect = element.element->rect;
	track_rect.shrink(handle_width / 2);
	track_rect.y = track_rect.y2();
	track_rect.h = track_height;

	bool hovered = element.element->rect.contains(keys::mouse_pos);
	bool active = active_element == &element;

	// Determine current value and range based on type
	float min_val = std::visit(
		[](auto&& arg) {
			return static_cast<float>(arg);
		},
		slider_data.min_value
	);
	float max_val = std::visit(
		[](auto&& arg) {
			return static_cast<float>(arg);
		},
		slider_data.max_value
	);

	if (hovered && keys::is_mouse_down())
		active_element = &element;

	hover_anim.set_goal(hovered || active ? 1.f : 0.f);

	if (active_element == &element) {
		if (keys::is_mouse_down()) {
			float mouse_progress = (keys::mouse_pos.x - track_rect.x) / (float)track_rect.w;
			mouse_progress = std::clamp(mouse_progress, 0.0f, 1.0f);

			float new_val = min_val + (mouse_progress * (max_val - min_val));

			// Apply snapping precision
			if (slider_data.precision > 0.0f) {
				new_val = std::round(new_val / slider_data.precision) * slider_data.precision;
			}

			// Snap to original type
			if (auto* ptr = std::get_if<int*>(&slider_data.current_value)) {
				**ptr = static_cast<int>(new_val);
			}
			else if (auto* ptr = std::get_if<float*>(&slider_data.current_value)) {
				**ptr = new_val;
			}

			// Trigger on_change callback if provided
			if (slider_data.on_change) {
				(*slider_data.on_change)(slider_data.current_value);
			}

			return true;
		}

		active_element = nullptr;
	}

	return false;
}

ui::Element& ui::add_slider(
	const std::string& id,
	Container& container,
	const std::variant<int, float>& min_value,
	const std::variant<int, float>& max_value,
	std::variant<int*, float*> value,
	const std::string& label_format,
	const SkFont& font,
	std::optional<std::function<void(const std::variant<int*, float*>&)>> on_change,
	float precision,
	const std::string& tooltip
) {
	int line_height = font.getSize();

	gfx::Size slider_size(200, line_height + (handle_width * 2));
	if (tooltip != "")
		slider_size.h += line_height_add + line_height;

	Element element = {
		.type = ElementType::SLIDER,
		.rect = gfx::Rect(container.current_position, slider_size),
		.data =
			SliderElementData{
				.min_value = min_value,
				.max_value = max_value,
				.current_value = value,
				.label_format = label_format,
				.font = font,
				.on_change = std::move(on_change),
				.precision = precision,
				.tooltip = tooltip,
			},
		.render_fn = render_slider,
		.update_fn = update_slider,
	};

	return *add_element(
		container,
		id,
		std::move(element),
		container.line_height,
		{
			{ hasher("main"), { .speed = 25.f } },
			{ hasher("hover"), { .speed = 40.f } },
		}
	);
}
