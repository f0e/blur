#include "../ui.h"
#include "../../render/render.h"
#include "../keys.h"

constexpr float SLIDER_ROUNDING = 4.0f;
constexpr float HANDLE_WIDTH = 10;
constexpr int TRACK_HEIGHT = 4;
constexpr int LINE_HEIGHT_ADD = 7;
constexpr int TRACK_LABEL_GAP = 10;
constexpr int TOOLTIP_GAP = 4;
const std::string TIED_ICON = "a"; // chain
constexpr int TIED_ICON_GAP = 3;
constexpr gfx::Size TIE_PAD(5, 3);
constexpr float TIE_ROUNDING = 4.0f;

// text input
constexpr int TEXT_INPUT_GAP = 10;

constexpr gfx::Color TEXT_COLOR(255, 255, 255, 255);

constexpr gfx::Color SELECTION_COLOR(100, 100, 200, 100);
constexpr gfx::Color COMPOSITION_TEXT_COLOR(200, 200, 255, 255); // For IME
constexpr gfx::Color COMPOSITION_BG_COLOR(60, 60, 60, 150);      // For IME

namespace {
	struct SliderPositions {
		gfx::Rect label_rect;
		gfx::Point tooltip_pos;
		gfx::Rect track_rect;

		gfx::Rect tied_icon_rect;
		gfx::Point tied_icon_pos;
		gfx::Point tied_text_pos;
	};

	SliderPositions get_slider_positions(
		const ui::Container& container, const ui::AnimatedElement& element, const ui::SliderElementData& slider_data
	) {
		int line_height = slider_data.font->height();

		gfx::Rect track_rect = element.element->rect;
		track_rect.h = TRACK_HEIGHT;

		gfx::Point label_pos = track_rect.origin();
		gfx::Rect label_rect(label_pos, gfx::Size(track_rect.w, line_height));

		gfx::Point tooltip_pos = track_rect.origin();

		gfx::Rect tied_icon_rect;
		gfx::Point tied_icon_pos;
		gfx::Point tied_text_pos;

		if (slider_data.is_tied_slider) {
			const int icon_size = fonts::icons.calc_size(TIED_ICON).w;
			const int text_size =
				slider_data.tied_text.empty() ? 0 : slider_data.font->calc_size(slider_data.tied_text).w;
			const int tie_text_height = std::max(slider_data.font->height(), fonts::icons.height());

			gfx::Size tied_size(
				icon_size + (slider_data.tied_text.empty() ? 0 : TIED_ICON_GAP + text_size) // no gap if empty text
					+ (TIE_PAD.w * 2),
				tie_text_height + (TIE_PAD.h * 2)
			);

			tied_icon_rect = gfx::Rect(track_rect.top_right() - gfx::Point(tied_size.w, 0), tied_size);
			tied_icon_rect.y -= tied_icon_rect.h / 2;
			tied_icon_rect.y += line_height / 2;

			tied_icon_pos = tied_icon_rect.top_left() + gfx::Point(TIE_PAD.w, TIE_PAD.h);
			tied_text_pos = tied_icon_pos + gfx::Point(icon_size + TIED_ICON_GAP, 0);

			// line_height = std::max(line_height, tied_icon_rect.h);

			label_rect.w = tied_icon_rect.x - label_rect.x - TEXT_INPUT_GAP;
		}

		if (!slider_data.tooltip.empty()) {
			tooltip_pos.y += line_height + TOOLTIP_GAP;
			track_rect.y += line_height + TOOLTIP_GAP;

			line_height = slider_data.font->height();
		}

		track_rect.y += line_height + TRACK_LABEL_GAP;

		return {
			.label_rect = label_rect,
			.tooltip_pos = tooltip_pos,
			.track_rect = track_rect,
			.tied_icon_rect = tied_icon_rect,
			.tied_icon_pos = tied_icon_pos,
			.tied_text_pos = tied_text_pos,
		};
	}

	auto to_float = [](auto&& arg) {
		return static_cast<float>(arg);
	};

	auto to_float_ptr = [](auto&& arg) {
		return static_cast<float>(*arg);
	};

	std::string format_label(std::variant<int*, float*> value_ptr, const std::string& label_format) {
		return std::visit(
			[&](auto&& arg) {
				return std::vformat(label_format, std::make_format_args(*arg));
			},
			value_ptr
		);
	}

	std::string get_value_string(std::variant<int*, float*> value_ptr, const std::string& label_format) {
		// extract format specifier
		std::regex re(R"(\{[^}]*\})");
		std::smatch match;
		std::string format_spec;

		if (std::regex_search(label_format, match, re))
			format_spec = match.str(); // e.g. "{:.2f}"
		else
			return "";

		return std::visit(
			[&](auto ptr) -> std::string {
				return std::vformat(format_spec, std::make_format_args(*ptr));
			},
			value_ptr
		);
	}
}

void ui::render_slider(const Container& container, const AnimatedElement& element) {
	const auto& slider_data = std::get<SliderElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;

	// i'm doing this in render because it needs to run when the other slider if updating and atm it wouldn't cause only
	// one element can update at once. not ideal, but idc
	// todo: refactor
	if (slider_data.is_tied_slider && slider_data.is_tied != nullptr) {
		if (slider_observers.contains(element.element->id)) {
			auto& observer = slider_observers[element.element->id];

			if (*slider_data.is_tied && slider_data.tied_value) {
				// Get current tied value
				float current_tied_val = std::visit(to_float_ptr, *slider_data.tied_value);

				if (observer.init && current_tied_val != observer.last_tied_value) {
					float proportion = current_tied_val / observer.last_tied_value;

					std::visit(
						[proportion](auto&& arg) {
							*arg = static_cast<std::decay_t<decltype(*arg)>>(*arg * proportion);
						},
						slider_data.current_value
					);
				}

				observer.last_tied_value = current_tied_val;
				observer.init = true;
			}
			else {
				observer.init = false;
			}
		}
	}

	// Get current value as float
	float current_val = std::visit(to_float_ptr, slider_data.current_value);
	float min_val = std::visit(to_float, slider_data.min_value);
	float max_val = std::visit(to_float, slider_data.max_value);

	// Normalize progress
	float progress = std::clamp((current_val - min_val) / (max_val - min_val), 0.f, 1.f);

	int track_shade = 40 + (20 * hover_anim);
	gfx::Color track_color(track_shade, track_shade, track_shade, anim * 255);
	gfx::Color filled_color = HIGHLIGHT_COLOR.adjust_alpha(anim);
	gfx::Color handle_border_color(0, 0, 0, anim * 50);
	gfx::Color text_color(255, 255, 255, anim * 255);
	gfx::Color tooltip_color(125, 125, 125, anim * 255);
	gfx::Color tie_stroke(125, 125, 125, anim * 255);
	gfx::Color tie_background(50, 50, 50, anim * 255);

	auto positions = get_slider_positions(container, element, slider_data);

	auto text_input_state = helpers::text_input::has_text_edit(element.element->id);
	if (text_input_state) {
		gfx::Rect clip_rect = positions.label_rect;
		gfx::Point text_pos = clip_rect.origin();

		auto& state = helpers::text_input::text_input_map.at(element.element->id);

		helpers::text_input::render_text(
			slider_data.text_input,
			state,
			text_pos,
			TEXT_COLOR.adjust_alpha(anim),
			clip_rect,
			"",
			{},
			SELECTION_COLOR.adjust_alpha(anim),
			COMPOSITION_TEXT_COLOR.adjust_alpha(anim),
			COMPOSITION_BG_COLOR.adjust_alpha(anim)
		);
	}
	else {
		std::string label = format_label(slider_data.current_value, slider_data.label_format);
		render::text(positions.label_rect.origin(), text_color, label, *slider_data.font);
	}

	// Render tooltip if provided
	if (!slider_data.tooltip.empty()) {
		render::text(positions.tooltip_pos, tooltip_color, slider_data.tooltip, *slider_data.font);
	}

	// Render tied icon
	if (slider_data.is_tied_slider && slider_data.is_tied != nullptr) {
		float tied_anim = element.animations.at(hasher("tied")).current;
		auto scoped = element.animations.at(hasher("tied"));

		render::rounded_rect_stroke(positions.tied_icon_rect, tie_background.adjust_alpha(tied_anim), TIE_ROUNDING);
		// render::rounded_rect_filled(positions.tied_icon_rect, tie_background.adjust_alpha(tied_anim), TIE_ROUNDING);

		auto tie_text_colour = gfx::Color::lerp(tooltip_color, text_color, tied_anim);
		// gfx::Color tie_text_colour = *slider_data.is_tied ? text_color : tooltip_color;

		render::text(positions.tied_icon_pos, tie_text_colour, TIED_ICON, fonts::icons);
		render::text(positions.tied_text_pos, tie_text_colour, slider_data.tied_text, *slider_data.font);
	}

	// Render track and filled portion
	render::rect_filled(positions.track_rect, track_color);

	gfx::Rect filled_rect = positions.track_rect;
	filled_rect.w *= progress;
	render::rect_filled(filled_rect, filled_color);

	// Render handle
	if (progress >= 0.f && progress <= 1.f) {
		float handle_x = positions.track_rect.x + (positions.track_rect.w * progress) - (HANDLE_WIDTH / 2);
		gfx::Rect handle_rect(
			handle_x, positions.track_rect.y - ((HANDLE_WIDTH - TRACK_HEIGHT) / 2), HANDLE_WIDTH, HANDLE_WIDTH
		);

		render::rounded_rect_filled(handle_rect.expand(1), handle_border_color, HANDLE_WIDTH);
		render::rounded_rect_filled(handle_rect, filled_color, HANDLE_WIDTH);
	}
}

bool ui::update_slider(const Container& container, AnimatedElement& element) {
	auto& slider_data = std::get<SliderElementData>(element.element->data);

	auto& hover_anim = element.animations.at(hasher("hover"));

	auto positions = get_slider_positions(container, element, slider_data);

	// Create slightly larger clickable area
	auto clickable_rect = element.element->rect;
	int extra = (HANDLE_WIDTH / 2) - (float(positions.track_rect.h) / 2);
	if (extra > 0)
		clickable_rect.h += extra;

	bool tie_hovered = false;
	bool label_hovered = false;

	// tie
	if (!get_active_element()) {
		// tie: handle clicks
		if (slider_data.is_tied_slider && slider_data.is_tied != nullptr) {
			auto& tied_anim = element.animations.at(hasher("tied"));

			tie_hovered = positions.tied_icon_rect.contains(keys::mouse_pos) && set_hovered_element(element);

			bool tie_clicked = false;

			if (tie_hovered) {
				set_cursor(SDL_SYSTEM_CURSOR_POINTER);

				if (keys::is_mouse_down()) {
					keys::on_mouse_press_handled(SDL_BUTTON_LEFT);
					*slider_data.is_tied = !(*slider_data.is_tied);

					tied_anim.set_goal(1.f);

					tie_clicked = true;
				}
			}

			float goal = 0.f;
			if (tie_hovered)
				goal = 0.65f;
			if (*slider_data.is_tied)
				goal = 1.f;

			tied_anim.set_goal(goal);

			if (tie_clicked)
				return true;
		}
	}

	// text input
	if (get_active_element() != &element || get_active_element_type() != "slider") {
		label_hovered = positions.label_rect.contains(keys::mouse_pos) && set_hovered_element(element);
		if (label_hovered) {
			set_cursor(SDL_SYSTEM_CURSOR_TEXT);
		}

		auto deactivate_slider_text_input = [&]() {
			if (helpers::text_input::has_text_edit(element.element->id)) {
				ui::helpers::text_input::remove_text_edit(element.element->id);

				if (get_active_element() == &element) {
					reset_active_element();
				}
			}
		};

		// text input: handle clicks
		if (keys::is_mouse_down()) {
			if (label_hovered) {
				keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

				if (!helpers::text_input::has_text_edit(element.element->id)) {
					set_active_element(element, "text input");

					slider_data.editing_text = get_value_string(slider_data.current_value, slider_data.label_format);

					slider_data.text_input = helpers::text_input::TextInputData{
						.text = &slider_data.editing_text,
						.font = slider_data.font,
					};

					auto& state = ui::helpers::text_input::add_text_edit(element.element->id, slider_data.text_input);
					state.active = true;

					SDL_Rect input_rect = {
						positions.label_rect.x, positions.label_rect.y, positions.label_rect.w, positions.label_rect.h
					};
					SDL_StartTextInput(container.window);
					SDL_SetTextInputArea(container.window, &input_rect, 0);

					helpers::text_input::select_all(&slider_data.text_input, &state.edit_state);
				}
				else {
					auto& state = helpers::text_input::text_input_map.at(element.element->id);

					// Use stb_textedit_click to set cursor position (relative to text start)
					helpers::text_input::click(
						&slider_data.text_input,
						&state.edit_state,
						keys::mouse_pos.x - positions.label_rect.x,
						keys::mouse_pos.y - positions.label_rect.y
					);
				}
			}
			else {
				deactivate_slider_text_input();
			}
		}

		if (helpers::text_input::has_text_edit(element.element->id)) {
			auto& state = helpers::text_input::text_input_map.at(element.element->id);

			if (state.active && get_active_element() != &element) {
				deactivate_slider_text_input();
			}

			bool active = get_active_element() == &element;

			if (active) {
				while (!text_event_queue.empty()) {
					auto& event = text_event_queue.front();

					// escape/enter to submit
					if (event.type == SDL_EVENT_KEY_DOWN) {
						if (event.key.scancode == SDL_SCANCODE_ESCAPE || event.key.scancode == SDL_SCANCODE_RETURN) {
							deactivate_slider_text_input();
							break;
						}
					}

					helpers::text_input::handle_text_input_event(slider_data.text_input, state, event);
					text_event_queue.erase(text_event_queue.begin());
				}

				// --- Handle Mouse Drag ---
				if (state.active && keys::is_mouse_dragging(SDL_BUTTON_LEFT)) {
					helpers::text_input::drag(
						&slider_data.text_input,
						&state.edit_state,
						keys::mouse_pos.x - positions.label_rect.x,
						keys::mouse_pos.y - positions.label_rect.y
					);
				}

				bool value_changed = false;

				std::visit(
					[&](auto* value_ptr) {
						if (value_ptr) {
							using T = std::remove_pointer_t<decltype(value_ptr)>;
							T old_value = *value_ptr;

							std::stringstream ss(slider_data.editing_text);
							T new_value{};
							if (ss >> new_value) {
								*value_ptr = new_value;
							}
							else {
								*value_ptr = T{ 0 }; // fallback to 0 or 0.0f
							}

							value_changed = (*value_ptr != old_value);
						}
					},
					slider_data.current_value
				);

				if (value_changed && slider_data.on_change) {
					(*slider_data.on_change)(slider_data.current_value);
				}
			}

			// Clamp cursor/selection just in case
			helpers::text_input::clamp(&slider_data.text_input, &state.edit_state);

			if (!state.active) {
				slider_data.editing_text = "";
			}

			return active;
		}
	}

	// slider
	bool hovered =
		!tie_hovered && !label_hovered && clickable_rect.contains(keys::mouse_pos) && set_hovered_element(element);
	bool active = get_active_element() == &element;

	hover_anim.set_goal(hovered || active ? 1.f : 0.f);

	if (hovered) {
		set_cursor(SDL_SYSTEM_CURSOR_POINTER);
		if (keys::is_mouse_down())
			set_active_element(element, "slider");
	}

	float min_val = std::visit(to_float, slider_data.min_value);
	float max_val = std::visit(to_float, slider_data.max_value);

	// slider: handle clicks
	if (get_active_element() == &element) {
		if (keys::is_mouse_down()) {
			float mouse_progress =
				positions.track_rect.mouse_percent_x(keys::pressing_keys.contains(SDL_Scancode::SDL_SCANCODE_LSHIFT));

			float new_val = min_val + (mouse_progress * (max_val - min_val));

			// Apply snapping precision
			if (slider_data.precision > 0.0f) {
				new_val = std::round(new_val / slider_data.precision) * slider_data.precision;
			}

			// Update value based on its type
			if (auto* ptr = std::get_if<int*>(&slider_data.current_value)) {
				**ptr = static_cast<int>(new_val);
			}
			else if (auto* ptr = std::get_if<float*>(&slider_data.current_value)) {
				**ptr = new_val;
			}

			// Call on_change callback if provided
			if (slider_data.on_change) {
				(*slider_data.on_change)(slider_data.current_value);
			}

			return true;
		}
		else {
			reset_active_element();
		}
	}

	return false;
}

void ui::remove_slider(AnimatedElement& element) {
	if (slider_observers.contains(element.element->id)) {
		slider_observers.erase(element.element->id);
	}
}

ui::AnimatedElement* ui::add_slider(
	const std::string& id,
	Container& container,
	const std::variant<int, float>& min_value,
	const std::variant<int, float>& max_value,
	std::variant<int*, float*> value,
	const std::string& label_format,
	const render::Font& font,
	std::optional<std::function<void(const std::variant<int*, float*>&)>> on_change,
	float precision,
	const std::string& tooltip
) {
	gfx::Size slider_size(200, font.height() + TRACK_LABEL_GAP + TRACK_HEIGHT);
	if (!tooltip.empty())
		slider_size.h += LINE_HEIGHT_ADD + font.height();

	Element element(
		id,
		ElementType::SLIDER,
		gfx::Rect(container.current_position, slider_size),
		SliderElementData{ .min_value = min_value,
	                       .max_value = max_value,
	                       .current_value = value,
	                       .label_format = label_format,
	                       .font = &font,
	                       .on_change = std::move(on_change),
	                       .precision = precision,
	                       .tooltip = tooltip,
	                       .is_tied_slider = false,
	                       .is_tied = nullptr,
	                       .tied_value = {} },
		render_slider,
		update_slider
	);

	return add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(25.f) },
			{ hasher("hover"), AnimationState(80.f) },
		}
	);
}

ui::AnimatedElement* ui::add_slider_tied(
	const std::string& id,
	Container& container,
	const std::variant<int, float>& min_value,
	const std::variant<int, float>& max_value,
	std::variant<int*, float*> value,
	const std::string& label_format,
	std::variant<int*, float*> tied_value,
	bool& is_tied,
	const std::string& tied_text,
	const render::Font& font,
	std::optional<std::function<void(const std::variant<int*, float*>&)>> on_change,
	float precision,
	const std::string& tooltip
) {
	gfx::Size slider_size(200, font.height() + TRACK_LABEL_GAP + TRACK_HEIGHT);
	if (!tooltip.empty())
		slider_size.h += LINE_HEIGHT_ADD + font.height();

	if (!slider_observers.contains(id)) {
		slider_observers.emplace(id, SliderObserver{});
	}

	Element element(
		id,
		ElementType::SLIDER,
		gfx::Rect(container.current_position, slider_size),
		SliderElementData{ .min_value = min_value,
	                       .max_value = max_value,
	                       .current_value = value,
	                       .label_format = label_format,
	                       .font = &font,
	                       .on_change = std::move(on_change),
	                       .precision = precision,
	                       .tooltip = tooltip,
	                       .is_tied_slider = true,
	                       .is_tied = &is_tied,
	                       .tied_value = tied_value,
	                       .tied_text = tied_text },
		render_slider,
		update_slider,
		remove_slider
	);

	return add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(25.f) },
			{ hasher("hover"), AnimationState(80.f) },
			{ hasher("tied"), AnimationState(80.f, is_tied ? 1.f : 0.f) },
		}
	);
}
