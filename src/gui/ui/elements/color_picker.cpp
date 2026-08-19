#include "../ui.h"
#include "../../render/render.h"
#include "../../fonts/icons.h"

#include "../keys.h"

const float BOX_ROUNDING = 5.f;
const gfx::Size BOX_PADDING(10, 5);
const int LABEL_GAP = 10;
const int SWATCH_GAP = 8;
const float SWATCH_ROUNDING = 3.f;
const int ARROW_PAD = 2;
const int ARROW_TEXT_GAP = 6;

const gfx::Color SELECTION_COLOR(100, 100, 200, 100);
const gfx::Color COMPOSITION_TEXT_COLOR(200, 200, 255, 255); // for IME
const gfx::Color COMPOSITION_BG_COLOR(60, 60, 60, 150);      // for IME

const int POPUP_GAP = 3;
const gfx::Size POPUP_PADDING(10, 10);
const int SECTION_GAP = 10;
const int SV_HEIGHT = 90;
const int HUE_HEIGHT = 10;
const int PRESET_SIZE = 16;
const int PRESET_GAP = 6;
const float PRESET_ROUNDING = 3.f;
const int SV_CURSOR_RADIUS = 4;
const int HUE_HANDLE_WIDTH = 3;
const int BORDER_SHADE = 70;

namespace {
	enum ColorPickerDrag : std::uint8_t {
		DRAG_NONE,
		DRAG_SV,
		DRAG_HUE,
	};

	struct Positions {
		gfx::Point label_pos;
		gfx::Rect box_rect;
		gfx::Rect swatch_rect;
		std::string text;        // what the box shows: the hex, "default", or what's being typed
		gfx::Rect text_rect;     // the space the text gets, where it's drawn & clipped
		gfx::Rect text_hit_rect; // just the text itself, the rest of the box belongs to the popup toggle

		gfx::Rect popup_rect;      // clipped to the expand animation
		gfx::Rect full_popup_rect; // where it sits when fully open, everything inside is laid out off this
		gfx::Rect sv_rect;
		gfx::Rect hue_rect;
		std::vector<gfx::Rect> preset_rects;
	};

	gfx::Color effective_color(const ui::ColorPickerElementData& data) {
		return gfx::Color::from_hex_string(*data.hex, false).value_or(data.default_color);
	}

	// pulls the picker's hsb back out of the bound hex string
	void sync_hsb(ui::ColorPickerElementData& data) {
		auto [hue, saturation, brightness] = effective_color(data).to_hsb();

		// a greyscale/black color has no hue or saturation to read, keep what's already picked so the
		// handles don't jump back to red when you drag the value down to nothing
		if (saturation > 0.f)
			data.hue = hue;
		if (brightness > 0.f)
			data.saturation = saturation;

		data.brightness = brightness;
		data.synced_hex = *data.hex;
	}

	Positions get_positions(
		const ui::Container& container,
		const ui::AnimatedElement& element,
		const ui::ColorPickerElementData& data,
		float anim = 1.f
	) {
		Positions positions;

		positions.label_pos = element.element->rect.origin();

		positions.box_rect = element.element->rect;
		positions.box_rect.y = positions.label_pos.y + data.font.height() + LABEL_GAP;
		positions.box_rect.h -= positions.box_rect.y - element.element->rect.y;

		int swatch_size = data.font.height() - 2;
		positions.swatch_rect = gfx::Rect(
			gfx::Point(positions.box_rect.x + BOX_PADDING.w, positions.box_rect.center().y - (swatch_size / 2)),
			gfx::Size(swatch_size, swatch_size)
		);

		int arrow_left =
			positions.box_rect.x2() - BOX_PADDING.w - ARROW_PAD - fonts::icons.calc_size(icons::DROPDOWN_ARROW).w;

		bool editing = ui::helpers::text_input::has_text_edit(element.element->id);

		positions.text = editing             ? data.editing_text
		                 : data.hex->empty() ? "default"
		                                     : effective_color(data).to_hex_string();

		positions.text_rect = gfx::Rect(
			gfx::Point(positions.swatch_rect.x2() + SWATCH_GAP, positions.box_rect.y + BOX_PADDING.h),
			gfx::Size(arrow_left - ARROW_TEXT_GAP - (positions.swatch_rect.x2() + SWATCH_GAP), data.font.height())
		);

		positions.text_hit_rect = positions.text_rect;
		positions.text_hit_rect.y = positions.box_rect.y;
		positions.text_hit_rect.h = positions.box_rect.h;

		// idle, only the text itself starts an edit, so there's still plenty of box left to open the popup
		// with. once it's a field, the whole of it is fair game for placing the caret
		if (!editing)
			positions.text_hit_rect.w = std::min(data.font.calc_size(positions.text).w, positions.text_rect.w);

		// popup contents
		int inner_width = element.element->rect.w - (POPUP_PADDING.w * 2);

		int per_row = std::max(1, (inner_width + PRESET_GAP) / (PRESET_SIZE + PRESET_GAP));
		int preset_rows =
			data.presets.empty() ? 0 : static_cast<int>((data.presets.size() + per_row - 1) / per_row); // ceil

		int popup_height = (POPUP_PADDING.h * 2) + SV_HEIGHT + SECTION_GAP + HUE_HEIGHT;
		if (preset_rows > 0)
			popup_height += SECTION_GAP + (preset_rows * PRESET_SIZE) + ((preset_rows - 1) * PRESET_GAP);

		positions.full_popup_rect = gfx::Rect(
			gfx::Point(element.element->rect.x, positions.box_rect.y2() + POPUP_GAP),
			gfx::Size(element.element->rect.w, popup_height)
		);

		bool open_upwards = positions.full_popup_rect.y2() + POPUP_GAP > container.get_usable_rect().y2() &&
		                    positions.box_rect.y - popup_height - POPUP_GAP > container.get_usable_rect().y;

		if (open_upwards)
			positions.full_popup_rect.y = positions.box_rect.y - popup_height - POPUP_GAP;

		positions.popup_rect = positions.full_popup_rect;
		positions.popup_rect.h = static_cast<int>(positions.full_popup_rect.h * anim);
		if (open_upwards)
			positions.popup_rect.y = positions.full_popup_rect.y2() - positions.popup_rect.h;

		gfx::Point content_pos = positions.full_popup_rect.origin() + gfx::Point(POPUP_PADDING.w, POPUP_PADDING.h);

		positions.sv_rect = gfx::Rect(content_pos, gfx::Size(inner_width, SV_HEIGHT));

		positions.hue_rect = gfx::Rect(
			gfx::Point(content_pos.x, positions.sv_rect.y2() + SECTION_GAP), gfx::Size(inner_width, HUE_HEIGHT)
		);

		gfx::Point preset_pos(content_pos.x, positions.hue_rect.y2() + SECTION_GAP);
		for (size_t i = 0; i < data.presets.size(); i++) {
			int column = static_cast<int>(i) % per_row;
			int row = static_cast<int>(i) / per_row;

			positions.preset_rects.emplace_back(
				gfx::Rect(
					gfx::Point(
						preset_pos.x + (column * (PRESET_SIZE + PRESET_GAP)),
						preset_pos.y + (row * (PRESET_SIZE + PRESET_GAP))
					),
					gfx::Size(PRESET_SIZE, PRESET_SIZE)
				)
			);
		}

		return positions;
	}

	void render_hue_strip(const gfx::Rect& rect, float alpha) {
		const int segments = 6;

		for (int i = 0; i < segments; i++) {
			gfx::Rect segment = rect;
			segment.x = rect.x + static_cast<int>(std::round(rect.w * (float(i) / segments)));
			segment.w = rect.x + static_cast<int>(std::round(rect.w * (float(i + 1) / segments))) - segment.x;

			render::rect_filled_gradient(
				segment,
				render::GradientDirection::GRADIENT_RIGHT,
				{
					gfx::Color::from_hsb(float(i) / segments, 1.f, 1.f).adjust_alpha(alpha),
					gfx::Color::from_hsb(float(i + 1) / segments, 1.f, 1.f).adjust_alpha(alpha),
				}
			);
		}
	}
}

void ui::render_color_picker(const Container& container, const AnimatedElement& element) {
	const auto& data = std::get<ColorPickerElementData>(element.element->data);

	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;
	float expand_anim = element.animations.at(hasher("expand")).current;
	float expand_goal = element.animations.at(hasher("expand")).goal;

	auto pos = get_positions(container, element, data, expand_anim);

	gfx::Color color = effective_color(data);

	int background_shade = 15 + (10 * hover_anim);

	gfx::Color background_color(background_shade, background_shade, background_shade, anim * 255);
	gfx::Color text_color(255, 255, 255, anim * 255);
	gfx::Color border_color(BORDER_SHADE, BORDER_SHADE, BORDER_SHADE, anim * 255);
	gfx::Color swatch_border_color = gfx::Color::white(anim * 40);
	gfx::Color arrow_colour(100, 100, 100, anim * 255);

	render::text(pos.label_pos, text_color, data.label, data.font);

	render::rounded_rect_filled(pos.box_rect, background_color, BOX_ROUNDING);
	render::rounded_rect_stroke(pos.box_rect, border_color, BOX_ROUNDING);

	render::rounded_rect_filled(pos.swatch_rect, color.adjust_alpha(anim), SWATCH_ROUNDING);
	render::rounded_rect_stroke(pos.swatch_rect, swatch_border_color, SWATCH_ROUNDING);

	if (helpers::text_input::has_text_edit(element.element->id)) {
		auto& state = helpers::text_input::text_input_map.at(element.element->id);

		helpers::text_input::render_text(
			data.text_input,
			state,
			pos.text_rect.origin(),
			text_color,
			pos.text_rect,
			"",
			{},
			SELECTION_COLOR.adjust_alpha(anim),
			COMPOSITION_TEXT_COLOR.adjust_alpha(anim),
			COMPOSITION_BG_COLOR.adjust_alpha(anim)
		);
	}
	else {
		render::text(pos.text_rect.origin(), text_color, pos.text, data.font);
	}

	gfx::Point arrow_pos(pos.box_rect.x2() - BOX_PADDING.w - ARROW_PAD, pos.box_rect.center().y);

	render::text(
		arrow_pos,
		arrow_colour,
		icons::DROPDOWN_ARROW,
		fonts::icons,
		FONT_CENTERED_X | FONT_CENTERED_Y,
		expand_goal * 180.f,
		17 // same fudge as the dropdown's arrow
	);

	if (expand_anim <= 0.01f)
		return;

	// the popup overlaps whatever's underneath it, so it has to be drawn after everything else
	gfx::Color popup_color(7, 7, 7, anim * 255);
	gfx::Color popup_border_color(BORDER_SHADE, BORDER_SHADE, BORDER_SHADE, anim * 255);
	gfx::Color handle_color = gfx::Color::white(anim * 255);
	gfx::Color handle_border_color = gfx::Color::black(anim * 130);
	gfx::Color hue_color = gfx::Color::from_hsb(data.hue, 1.f, 1.f);

	gfx::Point sv_cursor(
		pos.sv_rect.x + static_cast<int>(pos.sv_rect.w * data.saturation),
		pos.sv_rect.y + static_cast<int>(pos.sv_rect.h * (1.f - data.brightness))
	);

	int hue_handle_x = pos.hue_rect.x + static_cast<int>(pos.hue_rect.w * data.hue) - (HUE_HANDLE_WIDTH / 2);

	std::vector<gfx::Color> presets = data.presets;
	std::vector<gfx::Rect> preset_rects = pos.preset_rects;
	int hovered_preset = data.hovered_preset;
	gfx::Color selected_color = color;

	render::late_draw_calls.emplace_back([=] {
		render::rounded_rect_filled(pos.popup_rect, popup_color, BOX_ROUNDING);
		render::rounded_rect_stroke(pos.popup_rect, popup_border_color, BOX_ROUNDING);

		render::push_clip_rect(pos.popup_rect);

		// saturation/brightness square
		render::rect_filled_gradient(
			pos.sv_rect,
			render::GradientDirection::GRADIENT_RIGHT,
			{ gfx::Color::white(anim * 255), hue_color.adjust_alpha(anim) }
		);
		render::rect_filled_gradient(
			pos.sv_rect,
			render::GradientDirection::GRADIENT_DOWN,
			{ gfx::Color::black(0), gfx::Color::black(anim * 255) }
		);
		render::rect_stroke(pos.sv_rect, popup_border_color);

		render::circle_stroke(sv_cursor, SV_CURSOR_RADIUS + 1, handle_border_color);
		render::circle_stroke(sv_cursor, SV_CURSOR_RADIUS, handle_color);

		// hue strip
		render_hue_strip(pos.hue_rect, anim);
		render::rect_stroke(pos.hue_rect, popup_border_color);

		gfx::Rect hue_handle_rect(
			gfx::Point(hue_handle_x, pos.hue_rect.y - 2), gfx::Size(HUE_HANDLE_WIDTH, pos.hue_rect.h + 4)
		);

		render::rounded_rect_filled(hue_handle_rect.expand(1), handle_border_color, HUE_HANDLE_WIDTH);
		render::rounded_rect_filled(hue_handle_rect, handle_color, HUE_HANDLE_WIDTH);

		// presets
		for (size_t i = 0; i < presets.size(); i++) {
			const auto& preset_rect = preset_rects[i];
			bool selected = presets[i] == selected_color;

			render::rounded_rect_filled(preset_rect, presets[i].adjust_alpha(anim), PRESET_ROUNDING);

			gfx::Color preset_border_color = popup_border_color;
			if (selected)
				preset_border_color = gfx::Color::white(anim * 255);
			else if (static_cast<int>(i) == hovered_preset)
				preset_border_color = gfx::Color::white(anim * 150);

			render::rounded_rect_stroke(preset_rect, preset_border_color, PRESET_ROUNDING);
		}

		render::pop_clip_rect();
	});
}

bool ui::update_color_picker(const Container& container, AnimatedElement& element) {
	auto& data = std::get<ColorPickerElementData>(element.element->data);
	const std::string& id = element.element->id;

	auto& hover_anim = element.animations.at(hasher("hover"));
	auto& expand_anim = element.animations.at(hasher("expand"));

	bool editing = helpers::text_input::has_text_edit(id);

	if (editing) {
		// the element data gets rebuilt between frames, so point the edit at this frame's buffer rather than
		// trusting what was set when it started
		data.text_input.text = &data.editing_text;
		data.text_input.font = data.font;
		helpers::text_input::add_text_edit(id, data.text_input);
	}

	// something else took over, don't leave the popup hanging open behind it
	if (data.open && get_active_element() != &element) {
		data.open = false;
		data.drag_target = DRAG_NONE;
		expand_anim.set_goal(0.f);
	}

	// the hex can change from under us (config imported, defaults restored, changes reset)
	if (data.synced_hex != *data.hex)
		sync_hsb(data);

	auto pos = get_positions(container, element, data, expand_anim.current);

	// the popup draws over whatever's below it
	element.z_index = data.open ? 2 : (expand_anim.current > 0.01f ? 1 : 0);

	bool mouse_down = keys::is_mouse_down();
	bool mouse_pressed = mouse_down && !data.mouse_was_down;
	bool pressed_this_frame = mouse_pressed; // kept unclaimed for the text edit, which does its own hit testing
	data.mouse_was_down = mouse_down;

	bool text_hovered = pos.text_hit_rect.contains(keys::mouse_pos) && set_hovered_element(element);
	bool box_hovered = !text_hovered && pos.box_rect.contains(keys::mouse_pos) && set_hovered_element(element);

	hover_anim.set_goal(box_hovered || text_hovered || data.open || editing ? 1.f : 0.f);

	auto set_open = [&](bool open) {
		data.open = open;
		data.drag_target = DRAG_NONE;
		expand_anim.set_goal(open ? 1.f : 0.f);
		element.z_index = open ? 2 : 1; // still on top while it animates closed

		if (open)
			set_active_element(element, editing ? "text input" : "color picker");
		else if (!editing && get_active_element() == &element)
			reset_active_element();
	};

	auto start_editing = [&] {
		if (editing)
			return;

		editing = true;

		set_active_element(element, "text input");

		// a cleared hex shows as "default", but put the color it resolves to in the field so it can be copied
		data.editing_text = data.hex->empty() ? effective_color(data).to_hex_string() : *data.hex;

		data.text_input = helpers::text_input::TextInputData{
			.text = &data.editing_text,
			.font = data.font,
		};

		auto& state = helpers::text_input::add_text_edit(id, data.text_input);
		state.active = true;

		SDL_StartTextInput(container.window);

		helpers::text_input::select_all(&data.text_input, &state.edit_state);

		// don't let the drag that follows this same press wipe the select-all
		state.selected_all_mouse_lock = true;
	};

	auto stop_editing = [&] {
		if (!editing)
			return;

		editing = false;

		helpers::text_input::remove_text_edit(id);
		data.editing_text.clear();

		if (SDL_TextInputActive(container.window))
			SDL_StopTextInput(container.window);

		if (get_active_element() == &element) {
			// the popup outlives the text edit, hand it back its claim on the input
			if (data.open)
				set_active_element(element, "color picker");
			else
				reset_active_element();
		}
	};

	// straight off the handles, the hsb is what the user picked so it stays as-is
	auto write_picked_color = [&] {
		*data.hex = gfx::Color::from_hsb(data.hue, data.saturation, data.brightness).to_hex_string();
		data.synced_hex = *data.hex;

		if (data.on_change)
			(*data.on_change)();
	};

	// from a preset or a typed hex, so the handles have to follow it
	auto set_color = [&](const gfx::Color& color) {
		// the default color lives in the config as an empty value, keep it that way so it follows the default
		// if it ever changes
		std::string new_hex = color == data.default_color ? "" : color.to_hex_string();
		if (new_hex == *data.hex)
			return false;

		*data.hex = new_hex;
		sync_hsb(data);

		if (data.on_change)
			(*data.on_change)();

		return true;
	};

	bool updated = false;

	if (text_hovered)
		set_cursor(SDL_SYSTEM_CURSOR_TEXT);
	else if (box_hovered)
		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

	if (mouse_pressed) {
		if (text_hovered) {
			// claim it so the same click doesn't toggle the popup too
			keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

			start_editing();

			mouse_pressed = false;
			updated = true;
		}
		else {
			// a click anywhere else is done with the field
			stop_editing();

			if (box_hovered) {
				keys::on_mouse_press_handled(SDL_BUTTON_LEFT);
				set_open(!data.open);

				return true;
			}
		}
	}

	if (!data.open) {
		data.hovered_preset = -1;
	}
	else {
		bool popup_hovered = pos.full_popup_rect.contains(keys::mouse_pos) && set_hovered_element(element);

		data.hovered_preset = -1;
		if (popup_hovered) {
			for (size_t i = 0; i < pos.preset_rects.size(); i++) {
				if (pos.preset_rects[i].contains(keys::mouse_pos)) {
					data.hovered_preset = static_cast<int>(i);
					break;
				}
			}
		}

		if (!mouse_down)
			data.drag_target = DRAG_NONE;

		if (mouse_pressed) {
			if (pos.sv_rect.contains(keys::mouse_pos)) {
				data.drag_target = DRAG_SV;
			}
			else if (pos.hue_rect.contains(keys::mouse_pos)) {
				data.drag_target = DRAG_HUE;
			}
			else if (data.hovered_preset != -1) {
				set_color(data.presets[data.hovered_preset]);

				keys::on_mouse_press_handled(SDL_BUTTON_LEFT);
				set_open(false);

				return true;
			}
			else if (!popup_hovered && !text_hovered && !box_hovered) {
				set_open(false);

				return true;
			}
		}

		if (data.drag_target == DRAG_SV) {
			set_cursor(SDL_SYSTEM_CURSOR_CROSSHAIR);

			data.saturation = pos.sv_rect.mouse_percent_x();
			data.brightness = 1.f - pos.sv_rect.mouse_percent_y();

			write_picked_color();

			updated = true;
		}
		else if (data.drag_target == DRAG_HUE) {
			set_cursor(SDL_SYSTEM_CURSOR_CROSSHAIR);

			data.hue = std::min(pos.hue_rect.mouse_percent_x(), 0.9999f); // 1 wraps back round to 0

			write_picked_color();

			updated = true;
		}
		else if (popup_hovered) {
			if (data.hovered_preset != -1)
				set_cursor(SDL_SYSTEM_CURSOR_POINTER);
			else if (pos.sv_rect.contains(keys::mouse_pos) || pos.hue_rect.contains(keys::mouse_pos))
				set_cursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
		}
	}

	if (editing) {
		auto& state = helpers::text_input::text_input_map.at(id);

		while (!text_event_queue.empty()) {
			auto& event = text_event_queue.front();

			// enter/escape are done editing
			if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.scancode == SDL_SCANCODE_ESCAPE || event.key.scancode == SDL_SCANCODE_RETURN) {
					stop_editing();
					break;
				}
			}

			helpers::text_input::handle_text_input_event(data.text_input, state, event);
			text_event_queue.erase(text_event_queue.begin());
		}
	}

	if (editing) {
		auto& state = helpers::text_input::text_input_map.at(id);

		gfx::Point text_relative_pos(
			keys::mouse_pos.x - pos.text_rect.x + static_cast<int>(state.scroll_x), keys::mouse_pos.y - pos.text_rect.y
		);

		helpers::text_input::handle_mouse(
			data.text_input,
			state,
			text_relative_pos,
			text_hovered,
			pressed_this_frame,
			keys::get_click_count(),
			(SDL_GetModState() & SDL_KMOD_SHIFT) != 0u
		);

		helpers::text_input::update_ime_area(container.window, state, data.font);

		// apply whatever's been typed as soon as it's a whole color, so pasting one shows up right away.
		// half-typed values just leave the last good one alone
		auto typed = gfx::Color::from_hex_string(u::trim(data.editing_text), false);
		if (typed)
			updated |= set_color(*typed);

		helpers::text_input::clamp(&data.text_input, &state.edit_state);

		return true; // keep rendering, the caret blinks
	}

	return updated;
}

void ui::remove_color_picker(AnimatedElement& element) {
	helpers::text_input::remove_text_edit(element.element->id);

	if (get_active_element() == &element)
		reset_active_element();
}

bool ui::is_color_picker_open(const Container& container, const std::string& id) {
	auto it = container.elements.find(id);
	if (it == container.elements.end())
		return false;

	const auto* data = std::get_if<ColorPickerElementData>(&it->second.element->data);
	return data && data->open;
}

ui::AnimatedElement* ui::add_color_picker(
	const std::string& id,
	Container& container,
	const std::string& label,
	std::string& hex,
	const render::Font& font,
	gfx::Color default_color,
	const std::vector<gfx::Color>& presets,
	std::optional<std::function<void()>> on_change
) {
	gfx::Size total_size(
		container.get_usable_rect().w, font.height() + LABEL_GAP + font.height() + (BOX_PADDING.h * 2)
	);

	ColorPickerElementData data{
		.label = label,
		.hex = &hex,
		.font = font,
		.default_color = default_color,
		.presets = presets,
		.on_change = std::move(on_change),
	};

	sync_hsb(data);

	Element element(
		id,
		ElementType::COLOR_PICKER,
		gfx::Rect(container.current_position, total_size),
		std::move(data),
		render_color_picker,
		update_color_picker,
		remove_color_picker
	);

	return add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(25.f) },
			{ hasher("hover"), AnimationState(80.f) },
			{ hasher("expand"), AnimationState(30.f) },
		}
	);
}
