#include "../ui.h"
#include "../render.h"
#include "../utils.h"
#include "../keys.h"

#include "../../renderer.h"

const float SLIDER_ROUNDING = 4.0f;
const float HANDLE_WIDTH = 10;
const int TRACK_HEIGHT = 4;
const int LINE_HEIGHT_ADD = 7;
const int TRACK_LABEL_GAP = 10;

namespace {
	struct SliderPositions {
		gfx::Point label_pos;
		gfx::Point tooltip_pos;
		gfx::Rect track_rect;
	};

	SliderPositions get_slider_positions(const ui::AnimatedElement& element, const ui::SliderElementData& slider_data) {
		int text_size = render::get_font_height(slider_data.font);

		gfx::Point label_pos = element.element->rect.origin();
		label_pos.y += text_size;

		gfx::Rect track_rect = element.element->rect;
		track_rect.y = label_pos.y + TRACK_LABEL_GAP;
		track_rect.h = TRACK_HEIGHT;

		gfx::Point tooltip_pos = label_pos;
		if (slider_data.tooltip != "") {
			tooltip_pos.y += LINE_HEIGHT_ADD;
			tooltip_pos.y += text_size;

			// shift track rect down
			track_rect.y += tooltip_pos.y - label_pos.y;
		}

		return {
			.label_pos = label_pos,
			.tooltip_pos = tooltip_pos,
			.track_rect = track_rect,
		};
	}
}

void ui::render_slider(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	const auto& slider_data = std::get<SliderElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;

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

	gfx::Color filled_color = utils::adjust_color(HIGHLIGHT_COLOR, anim);
	gfx::Color handle_border_color = utils::adjust_color(gfx::rgba(0, 0, 0, 50), anim);

	gfx::Color text_color = utils::adjust_color(gfx::rgba(255, 255, 255, 255), anim);
	gfx::Color tooltip_color = utils::adjust_color(gfx::rgba(125, 125, 125, 255), anim);

	auto positions = get_slider_positions(element, slider_data);

	std::string label;
	std::visit(
		[&](auto&& arg) {
			label = std::vformat(slider_data.label_format, std::make_format_args(*arg));
		},
		slider_data.current_value
	);

	render::text(surface, positions.label_pos, text_color, label, slider_data.font, os::TextAlign::Left);

	if (slider_data.tooltip != "") {
		render::text(
			surface, positions.tooltip_pos, tooltip_color, slider_data.tooltip, slider_data.font, os::TextAlign::Left
		);
	}

	// Draw track
	render::rect_filled(surface, positions.track_rect, track_color);

	// Draw filled portion
	gfx::Rect filled_rect = positions.track_rect;
	filled_rect.w *= clamped_progress;
	render::rect_filled(surface, filled_rect, filled_color);

	// Calculate handle position
	if (progress >= 0.f && progress <= 1.f) {
		float handle_x = positions.track_rect.x + (positions.track_rect.w * clamped_progress) - (HANDLE_WIDTH / 2);
		gfx::Rect handle_rect(
			handle_x, positions.track_rect.y - ((HANDLE_WIDTH - TRACK_HEIGHT) / 2), HANDLE_WIDTH, HANDLE_WIDTH
		);

		// Draw handle
		auto tmp = handle_rect;
		tmp.enlarge(1);
		render::rounded_rect_filled(surface, tmp, handle_border_color, HANDLE_WIDTH);
		render::rounded_rect_filled(surface, handle_rect, filled_color, HANDLE_WIDTH);
	}
}

bool ui::update_slider(const Container& container, AnimatedElement& element) {
	auto& slider_data = std::get<SliderElementData>(element.element->data);
	auto& hover_anim = element.animations.at(hasher("hover"));

	auto positions = get_slider_positions(element, slider_data);

	auto clickable_rect = element.element->rect;
	int extra = (HANDLE_WIDTH / 2) - int(positions.track_rect.h / 2);
	if (extra > 0)
		clickable_rect.h += extra;

	bool hovered = clickable_rect.contains(keys::mouse_pos) && set_hovered_element(element);
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

	if (hovered) {
		set_cursor(os::NativeCursor::Link);

		if (keys::is_mouse_down())
			active_element = &element;
	}

	hover_anim.set_goal(hovered || active ? 1.f : 0.f);

	if (active_element == &element) {
		if (keys::is_mouse_down()) {
			float mouse_progress = (keys::mouse_pos.x - positions.track_rect.x) / (float)positions.track_rect.w;
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
	int text_size = render::get_font_height(font);

	gfx::Size slider_size(200, text_size + TRACK_LABEL_GAP + TRACK_HEIGHT);
	if (tooltip != "")
		slider_size.h += LINE_HEIGHT_ADD + text_size;

	Element element(
		id,
		ElementType::SLIDER,
		gfx::Rect(container.current_position, slider_size),
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
		render_slider,
		update_slider
	);

	return *add_element(
		container,
		std::move(element),
		container.line_height,
		{
			{ hasher("main"), { .speed = 25.f } },
			{ hasher("hover"), { .speed = 80.f } },
		}
	);
}
