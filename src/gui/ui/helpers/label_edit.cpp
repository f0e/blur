#include "../ui.h"
#include "../keys.h"
#include "../../sdl.h"

constexpr gfx::Color SELECTION_COLOR(100, 100, 200, 100);
constexpr gfx::Color COMPOSITION_TEXT_COLOR(200, 200, 255, 255); // For IME
constexpr gfx::Color COMPOSITION_BG_COLOR(60, 60, 60, 150);      // For IME

void ui::begin_label_edit(
	const Container& container,
	AnimatedElement& element,
	helpers::text_input::TextInputData& input_data,
	const std::string& text
) {
	set_active_element(element, "text input");

	auto& state = helpers::text_input::add_text_edit(element.element->id, input_data);
	state.active = true;
	state.owned_text = text;

	input_data.text = &state.owned_text;

	SDL_StartTextInput(container.window);

	helpers::text_input::select_all(&input_data, &state.edit_state);

	// so the drag from this press doesn't clear the select-all
	state.selected_all_mouse_lock = true;
}

void ui::end_label_edit(AnimatedElement& element) {
	helpers::text_input::remove_text_edit(element.element->id);

	if (get_active_element() == &element)
		reset_active_element();

	// focus may have already moved to another text edit, which still needs it
	auto* active = get_active_element();
	if (active != nullptr && helpers::text_input::has_active_text_edit(active->element->id))
		return;

	if (SDL_TextInputActive(sdl::window))
		SDL_StopTextInput(sdl::window);
}

bool ui::update_label_edit(
	const Container& container,
	AnimatedElement& element,
	helpers::text_input::TextInputData& input_data,
	const gfx::Rect& rect,
	const std::function<void(const std::string&)>& on_change
) {
	const auto& id = element.element->id;
	if (!helpers::text_input::has_text_edit(id))
		return false;

	auto& state = helpers::text_input::text_input_map.at(id);

	bool hovered = rect.contains(keys::mouse_pos) && set_hovered_element(element);
	if (hovered)
		set_cursor(SDL_SYSTEM_CURSOR_TEXT);

	// grab this before claiming the press below, which clears it
	bool pressed = keys::is_mouse_down();

	if (get_active_element() != &element || (pressed && !hovered)) {
		end_label_edit(element);
		return false;
	}

	if (pressed)
		keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

	// the element data is replaced whenever its options change, so the text lives in the edit state
	input_data.text = &state.owned_text;
	helpers::text_input::add_text_edit(id, input_data);

	std::string old_text = state.owned_text;

	while (!text_event_queue.empty()) {
		auto event = text_event_queue.front();
		text_event_queue.erase(text_event_queue.begin());

		// escape/enter to submit
		if (event.type == SDL_EVENT_KEY_DOWN &&
		    (event.key.scancode == SDL_SCANCODE_ESCAPE || event.key.scancode == SDL_SCANCODE_RETURN))
		{
			end_label_edit(element);
			return true;
		}

		helpers::text_input::handle_text_input_event(input_data, state, event);
	}

	gfx::Point text_relative_pos(
		keys::mouse_pos.x - rect.x + static_cast<int>(state.scroll_x), keys::mouse_pos.y - rect.y
	);

	helpers::text_input::handle_mouse(
		input_data,
		state,
		text_relative_pos,
		hovered,
		pressed,
		keys::get_click_count(),
		(SDL_GetModState() & SDL_KMOD_SHIFT) != 0u
	);

	helpers::text_input::update_ime_area(container.window, state, input_data.font);

	if (state.owned_text != old_text)
		on_change(state.owned_text);

	// Clamp cursor/selection just in case
	helpers::text_input::clamp(&input_data, &state.edit_state);

	return true;
}

void ui::render_label_edit(
	const AnimatedElement& element,
	const helpers::text_input::TextInputData& input_data,
	const gfx::Rect& rect,
	const gfx::Color& color,
	float anim
) {
	auto& state = helpers::text_input::text_input_map.at(element.element->id);

	auto text_input = input_data;
	text_input.text = &state.owned_text;

	helpers::text_input::render_text(
		text_input,
		state,
		rect.origin(),
		color,
		rect,
		"",
		{},
		SELECTION_COLOR.adjust_alpha(anim),
		COMPOSITION_TEXT_COLOR.adjust_alpha(anim),
		COMPOSITION_BG_COLOR.adjust_alpha(anim)
	);
}
