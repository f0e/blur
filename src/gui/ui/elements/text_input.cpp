#include "../ui.h"
#include "../../render/render.h"
#include "../keys.h"
#include "../helpers/text_input.h"

constexpr int LABEL_GAP = 10;
constexpr float TEXT_INPUT_ROUNDING = 2.5f;
constexpr gfx::Size TEXT_INPUT_PADDING(10, 5);
constexpr gfx::Color BORDER_COLOR(70, 70, 70, 255);
constexpr gfx::Color ACTIVE_BORDER_COLOR(90, 90, 90, 255);
constexpr gfx::Color BG_COLOR(20, 20, 20, 200);
constexpr gfx::Color TEXT_COLOR(255, 255, 255, 255);
constexpr gfx::Color READ_ONLY_TEXT_COLOR(255, 255, 255, 155);
constexpr gfx::Color PLACEHOLDER_COLOR(150, 150, 150, 180);

const std::string EMPTY_PLACEHOLDER = "...";

constexpr gfx::Color SELECTION_COLOR(100, 100, 200, 100);
constexpr gfx::Color COMPOSITION_TEXT_COLOR(200, 200, 255, 255); // For IME
constexpr gfx::Color COMPOSITION_BG_COLOR(60, 60, 60, 150);      // For IME

namespace {
	struct Positions {
		gfx::Point label_pos;
		gfx::Rect input_rect;
		gfx::Point text_pos;
	};

	Positions get_positions(
		const ui::Container& container, const ui::TextInputElementData& input_data, const ui::AnimatedElement& element
	) {
		gfx::Point label_pos = element.element->rect.origin();

		auto input_rect = element.element->rect;
		if (!input_data.label.empty()) {
			int label_offset = input_data.text_input.font.height() + LABEL_GAP;
			input_rect.y += label_offset;
			input_rect.h -= label_offset;
		}

		gfx::Point text_pos(input_rect.x + TEXT_INPUT_PADDING.w, input_rect.y + TEXT_INPUT_PADDING.h);

		return {
			.label_pos = label_pos,
			.input_rect = input_rect,
			.text_pos = text_pos,
		};
	}
}

void ui::render_text_input(const Container& container, const AnimatedElement& element) {
	const auto& input_data = std::get<TextInputElementData>(element.element->data);

	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;
	float focus_anim = element.animations.at(hasher("focus")).current;

	auto pos = get_positions(container, input_data, element);
	auto& state = helpers::text_input::text_input_map.at(element.element->id);

	// --- Render Label ---
	if (!input_data.label.empty()) {
		render::text(pos.label_pos, TEXT_COLOR.adjust_alpha(anim), input_data.label, input_data.text_input.font);
	}

	// --- Render Background and Border ---
	gfx::Color current_bg_color = BG_COLOR.adjust_alpha(anim);
	render::rounded_rect_filled(pos.input_rect, current_bg_color, TEXT_INPUT_ROUNDING);

	gfx::Color current_border_color =
		gfx::Color::lerp(BORDER_COLOR, ACTIVE_BORDER_COLOR, focus_anim).adjust_alpha(anim);
	render::rounded_rect_stroke(pos.input_rect, current_border_color, TEXT_INPUT_ROUNDING);

	// --- Clipping ---
	gfx::Rect clip_rect = pos.input_rect;
	clip_rect.x += TEXT_INPUT_PADDING.w;
	clip_rect.y += TEXT_INPUT_PADDING.h;
	clip_rect.w -= TEXT_INPUT_PADDING.w * 2;
	clip_rect.h -= TEXT_INPUT_PADDING.h * 2;
	clip_rect.w = std::max(clip_rect.w, 0);
	clip_rect.h = std::max(clip_rect.h, 0);

	std::string placeholder = input_data.placeholder;
	if (placeholder.empty() && !input_data.text_input.read_only)
		placeholder = EMPTY_PLACEHOLDER;

	helpers::text_input::render_text(
		input_data.text_input,
		state,
		pos.text_pos,
		(input_data.text_input.read_only ? READ_ONLY_TEXT_COLOR : TEXT_COLOR).adjust_alpha(anim),
		clip_rect,
		placeholder,
		PLACEHOLDER_COLOR.adjust_alpha(anim),
		SELECTION_COLOR.adjust_alpha(anim),
		COMPOSITION_TEXT_COLOR.adjust_alpha(anim),
		COMPOSITION_BG_COLOR.adjust_alpha(anim)
	);
}

bool ui::update_text_input(const Container& container, AnimatedElement& element) {
	auto& input_data = std::get<TextInputElementData>(element.element->data);
	auto& hover_anim = element.animations.at(hasher("hover"));
	auto& focus_anim = element.animations.at(hasher("focus"));

	auto pos = get_positions(container, input_data, element);
	auto& state = helpers::text_input::text_input_map.at(element.element->id);

	bool hovered = pos.input_rect.contains(keys::mouse_pos) && set_hovered_element(element);
	hover_anim.set_goal(hovered ? 1.f : 0.f);

	if (hovered) {
		set_cursor(SDL_SYSTEM_CURSOR_TEXT);
	}

	// grab this before the focus handling below claims the press, which clears it
	bool pressed = keys::is_mouse_pressed();

	// Handle focus/unfocus with mouse click
	if (pressed) {
		if (hovered) {
			if (!state.active) {
				set_active_element(element);
				state.active = true;
				focus_anim.set_goal(1.f);

				// read only inputs are only there to be selected & copied, no need for ime
				if (!input_data.text_input.read_only) {
					SDL_StartTextInput(container.window);
				}
			}

			// claim the click every time, not just on the frame we take focus. leaving it unclaimed kept the
			// button in the "pressed" set, so is_mouse_pressed() stayed true and we re-clicked (resetting the
			// selection anchor) every frame instead of dragging
			keys::on_mouse_press_handled(SDL_BUTTON_LEFT);
		}
		else if (get_active_element() == &element) {
			reset_active_element();
		}
	}

	// Handle deactivation
	if (state.active && get_active_element() != &element) {
		state.active = false;
		focus_anim.set_goal(0.f);

		// Stop SDL text input if not in another text input
		if (get_active_element() && get_active_element()->element->type != ElementType::TEXT_INPUT) {
			if (SDL_TextInputActive(container.window)) {
				SDL_StopTextInput(container.window);
			}
		}
	}

	bool active = get_active_element() == &element;

	if (active) {
		while (!text_event_queue.empty()) {
			auto& event = text_event_queue.front();
			helpers::text_input::handle_text_input_event(input_data.text_input, state, event);
			text_event_queue.erase(text_event_queue.begin());
		}

		// mouse position in text space: undo the field origin, then re-apply the scroll offset that render_text
		// subtracts, so hit testing lines up with what's actually on screen
		gfx::Point text_relative_pos(
			keys::mouse_pos.x - pos.text_pos.x + static_cast<int>(state.scroll_x), keys::mouse_pos.y - pos.text_pos.y
		);

		helpers::text_input::handle_mouse(
			input_data.text_input,
			state,
			text_relative_pos,
			hovered,
			pressed,
			keys::get_click_count(),
			(SDL_GetModState() & SDL_KMOD_SHIFT) != 0u
		);

		if (!input_data.text_input.read_only)
			helpers::text_input::update_ime_area(container.window, state, input_data.text_input.font);
	}

	// Clamp cursor/selection just in case
	helpers::text_input::clamp(&input_data.text_input, &state.edit_state);

	return active;
}

int ui::text_input_height(const render::Font& font) {
	return font.height() + (TEXT_INPUT_PADDING.h * 2);
}

ui::AnimatedElement* ui::add_text_input(
	const std::string& id,
	Container& container,
	std::string& text, // Reference to the external string
	const std::string& label,
	const render::Font& font,
	const std::string& placeholder,
	std::optional<std::function<void(const std::string&)>> on_change,
	bool read_only,
	std::optional<int> width
) {
	helpers::text_input::TextInputData state;
	state.text = &text;
	state.font = font;
	state.on_change = std::move(on_change);
	state.read_only = read_only;

	helpers::text_input::add_text_edit(id, state);

	gfx::Size total_size(width.value_or(container.get_usable_rect().w), text_input_height(font));
	if (!label.empty())
		total_size.h += font.height() + LABEL_GAP;

	Element element(
		id,
		ElementType::TEXT_INPUT,
		gfx::Rect(container.current_position, total_size),
		TextInputElementData{
			.text_input = std::move(state),
			.label = label,
			.placeholder = placeholder,
		},
		render_text_input,
		update_text_input
	);

	return add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(25.f) },
			{ hasher("hover"), AnimationState(80.f) },
			{ hasher("focus"), AnimationState(25.f) },
		}
	);
}
