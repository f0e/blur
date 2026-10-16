#include "../ui.h"
#include "../../render/render.h"
#include "../keys.h"

// the fps axis is logarithmic so low rates aren't squeezed into the first few pixels
constexpr float AXIS_MIN = 30.f;
constexpr float AXIS_MAX = 3000.f;

constexpr int TRACK_HEIGHT = 4;
constexpr float THUMB_RADIUS = 6.f;
constexpr int VALUE_GAP = 6;
constexpr int LABEL_GAP = 8;
constexpr int EDIT_CURSOR_ROOM = 4;
constexpr int SOURCE_MARK_HEIGHT = 12;
constexpr float REAL_HANDOVER_RADIUS = 3.5f;
// the first pass's part of the track and its thumb are dimmed so they don't outshine the main part, its text stays full
constexpr float PRE_SPAN_BRIGHTNESS = 0.55f;

// dragged thumbs are pulled onto these within SNAP_DISTANCE (a share of the track), and move in steps of 10 fps
// (5 under 100) everywhere else
constexpr std::array SNAP_FPS = { 60.f,  120.f, 144.f, 180.f,  240.f,  300.f,  360.f,  480.f,
	                              600.f, 720.f, 960.f, 1200.f, 1440.f, 1800.f, 2400.f, 3000.f };
constexpr float SNAP_DISTANCE = 0.02f;

namespace {
	enum class Thumb : std::uint8_t {
		NONE,
		PRE,
		FINAL,
	};

	// what's being dragged or edited, kept outside the element data since that's replaced when the options change
	struct TrackState {
		Thumb dragging = Thumb::NONE;
		Thumb editing = Thumb::NONE;
	};

	std::unordered_map<std::string, TrackState> track_states;

	float to_axis(float fps) {
		return std::log(std::clamp(fps, AXIS_MIN, AXIS_MAX) / AXIS_MIN) / std::log(AXIS_MAX / AXIS_MIN);
	}

	float from_axis(float position) {
		return AXIS_MIN * std::pow(AXIS_MAX / AXIS_MIN, std::clamp(position, 0.f, 1.f));
	}

	float snap(float fps) {
		float position = to_axis(fps);
		auto nearest = std::ranges::min_element(SNAP_FPS, {}, [&](float snap_fps) {
			return std::abs(to_axis(snap_fps) - position);
		});

		if (std::abs(to_axis(*nearest) - position) < SNAP_DISTANCE)
			return *nearest;

		float step = fps < 100.f ? 5.f : 10.f;
		return std::round(fps / step) * step;
	}

	// the interpolated fps setting is either an fps or a multiple of the source, like "5x"
	std::optional<float> final_multiplier(const ui::InterpolationTrackElementData& data) {
		if (!data.interpolated_fps->ends_with('x'))
			return {};

		try {
			return std::stof(data.interpolated_fps->substr(0, data.interpolated_fps->size() - 1));
		}
		catch (const std::exception&) {
			return {};
		}
	}

	// a multiple with no known source has no fps, so it's drawn at the end of the track
	float final_fps(const ui::InterpolationTrackElementData& data) {
		if (auto multiplier = final_multiplier(data))
			return data.source_fps ? static_cast<float>(*data.source_fps) * *multiplier : AXIS_MAX;

		try {
			return std::stof(*data.interpolated_fps);
		}
		catch (const std::exception&) {
			return 0.f;
		}
	}

	std::string final_text(const ui::InterpolationTrackElementData& data) {
		if (final_multiplier(data))
			return *data.interpolated_fps;

		return std::format("{} fps", *data.interpolated_fps);
	}

	std::string handover_text(const ui::InterpolationTrackElementData& data) {
		std::vector<std::string> parts;
		if (*data.pre_minimum_fps > 0)
			parts.push_back(std::format("{} fps", *data.pre_minimum_fps));
		if (*data.pre_minimum_multiplier > 1.f)
			parts.push_back(std::format("{}x", *data.pre_minimum_multiplier));

		return parts.empty() ? "-" : u::join(parts, ", ");
	}

	// "300", "300 fps", "2x" or "300 fps, 2x". whatever's left out is turned off
	void parse_handover(const ui::InterpolationTrackElementData& data, const std::string& text) {
		static const std::regex number_re(R"((\d+(?:\.\d+)?)\s*(x|fps)?)", std::regex::icase);

		int minimum_fps = 0;
		float multiplier = 1.f;
		bool found = false;

		for (auto it = std::sregex_iterator(text.begin(), text.end(), number_re); it != std::sregex_iterator(); ++it) {
			found = true;
			float value = std::stof((*it)[1].str());

			if (u::to_lower((*it)[2].str()) == "x")
				multiplier = std::max(value, 1.f);
			else
				minimum_fps = static_cast<int>(std::lround(value));
		}

		if (!found)
			return;

		*data.pre_minimum_fps = minimum_fps;
		*data.pre_minimum_multiplier = multiplier;
	}

	// "1200", "1200 fps" or a multiple of the source like "5x"
	void parse_final(const ui::InterpolationTrackElementData& data, const std::string& text) {
		static const std::regex final_re(R"(\s*(\d+(?:\.\d+)?)\s*(x|fps)?\s*)", std::regex::icase);

		std::smatch match;
		if (!std::regex_match(text, match, final_re))
			return;

		float value = std::stof(match[1].str());

		if (u::to_lower(match[2].str()) == "x") {
			if (value > 1.f)
				*data.interpolated_fps = std::format("{}x", value);
		}
		else if (value >= 1.f) {
			*data.interpolated_fps = std::to_string(std::lround(value));
		}
	}

	struct Layout {
		gfx::Rect track; // inset so the thumbs fit
		int center_y{};

		int start_x{}; // the source, or the left end when it isn't known
		int handover_x{};
		int pre_end_x{}; // where the handover really lands for the known source, otherwise the thumb
		int end_x{};
		std::optional<int> real_handover_x;

		gfx::Rect pre_value;
		gfx::Rect final_value;
	};

	// centred over their thumbs when there's room. as the thumbs close in, the wider label gives way first and then
	// both move out together, so each ends up the same distance from its own thumb
	std::pair<float, float> pair_labels(int a_center, int a_width, int b_center, int b_width, int left, int right) {
		float a_half = static_cast<float>(a_width) / 2;
		float b_half = static_cast<float>(b_width) / 2;
		float narrow_half = std::min(a_half, b_half);
		float wide_half = std::max(a_half, b_half);

		// how far each label's inner edge reaches past its thumb
		auto room = static_cast<float>(b_center - a_center - LABEL_GAP);
		float reach = room / 2;
		if (room >= narrow_half + wide_half)
			reach = wide_half;
		else if (room >= 2 * narrow_half)
			reach = room - narrow_half;

		float a = static_cast<float>(a_center) + std::min(a_half, reach) - a_half;
		float b = static_cast<float>(b_center) - std::min(b_half, reach) + b_half;

		// at the ends, the label that's held in pushes the other along
		float min_distance = a_half + b_half + LABEL_GAP;
		a = std::clamp(a, left + a_half, right - a_half);
		b = std::clamp(std::max(b, a + min_distance), left + b_half, right - b_half);
		a = std::min(a, b - min_distance);

		return { a, b };
	}

	gfx::Rect text_rect(float center_x, int y, const std::string& text, const render::Font& font) {
		int width = font.calc_size(text).w;
		return { static_cast<int>(std::lround(center_x)) - (width / 2), y, width, font.height() };
	}

	Layout get_layout(const ui::AnimatedElement& element, const ui::InterpolationTrackElementData& data) {
		const gfx::Rect& rect = element.element->rect;
		const auto& font = data.font;
		auto radius = static_cast<int>(THUMB_RADIUS);

		Layout layout;
		layout.center_y = rect.y + radius;
		int values_y = layout.center_y + radius + VALUE_GAP;
		layout.track =
			gfx::Rect(rect.x + radius, layout.center_y - (TRACK_HEIGHT / 2), rect.w - (radius * 2), TRACK_HEIGHT);

		auto x_of = [&](float fps) {
			return layout.track.x + static_cast<int>(std::lround(to_axis(fps) * static_cast<float>(layout.track.w)));
		};

		float end_fps = final_fps(data);
		layout.end_x = x_of(end_fps);
		layout.start_x = data.source_fps ? x_of(static_cast<float>(*data.source_fps)) : layout.track.x;

		// with only a multiple there's no fps to put the handover at, so it rests at the left
		layout.handover_x = *data.pre_minimum_fps > 0
		                        ? x_of(std::min(static_cast<float>(*data.pre_minimum_fps), end_fps))
		                        : layout.track.x;

		if (data.source_fps) {
			auto source = static_cast<float>(*data.source_fps);
			float target = std::max(source * *data.pre_minimum_multiplier, static_cast<float>(*data.pre_minimum_fps));
			layout.real_handover_x = x_of(std::min(target, end_fps));
		}

		layout.pre_end_x = data.show_pre ? std::max(layout.start_x, layout.real_handover_x.value_or(layout.handover_x))
		                                 : layout.start_x;

		std::string end_text = final_text(data);
		int end_width = font.calc_size(end_text).w;

		if (!data.show_pre) {
			float half = static_cast<float>(end_width) / 2;
			float x = std::clamp(
				static_cast<float>(layout.end_x),
				static_cast<float>(rect.x) + half,
				static_cast<float>(rect.x2()) - half
			);
			layout.final_value = text_rect(x, values_y, end_text, font);
			return layout;
		}

		std::string pre_text = handover_text(data);
		auto [pre_x, end_x] =
			pair_labels(layout.handover_x, font.calc_size(pre_text).w, layout.end_x, end_width, rect.x, rect.x2());
		layout.pre_value = text_rect(pre_x, values_y, pre_text, font);
		layout.final_value = text_rect(end_x, values_y, end_text, font);

		return layout;
	}

	// sized to what's been typed so far, so clicking anywhere off the text ends the edit
	gfx::Rect get_edit_rect(
		const ui::AnimatedElement& element,
		const ui::InterpolationTrackElementData& data,
		const Layout& layout,
		Thumb thumb
	) {
		const gfx::Rect& value = thumb == Thumb::PRE ? layout.pre_value : layout.final_value;
		const gfx::Rect& rect = element.element->rect;

		int text_width = value.w;
		if (auto it = ui::helpers::text_input::text_input_map.find(element.element->id);
		    it != ui::helpers::text_input::text_input_map.end())
			text_width = data.font.calc_size(it->second.owned_text).w;

		int width = std::min(text_width + EDIT_CURSOR_ROOM, rect.w);
		int x = std::clamp(value.center().x - (width / 2), rect.x, rect.x2() - width);
		return { x, value.y, width, value.h };
	}

	void drag_to(const ui::InterpolationTrackElementData& data, Thumb thumb, float fps) {
		if (thumb == Thumb::PRE)
			*data.pre_minimum_fps = static_cast<int>(std::min(fps, final_fps(data)));
		else
			*data.interpolated_fps = std::to_string(static_cast<int>(fps));
	}
}

void ui::render_interpolation_track(const Container& container, const AnimatedElement& element) {
	const auto& data = std::get<InterpolationTrackElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;

	auto layout = get_layout(element, data);
	auto state_it = track_states.find(element.element->id);
	Thumb editing = state_it != track_states.end() ? state_it->second.editing : Thumb::NONE;

	int track_shade = 40 + static_cast<int>(20 * hover_anim);
	gfx::Color final_color = highlight_color.adjust_alpha(anim);
	gfx::Color pre_color = PRE_INTERPOLATION_COLOR.adjust_alpha(anim);
	gfx::Color pre_track_color = gfx::Color::lerp(gfx::Color::black(pre_color.a), pre_color, PRE_SPAN_BRIGHTNESS);
	gfx::Color text_color(255, 255, 255, anim * 255);

	render::rect_filled(layout.track, gfx::Color(track_shade, track_shade, track_shade, anim * 255));

	auto span = [&](int from, int to, const gfx::Color& color) {
		if (to > from)
			render::rect_filled(gfx::Rect(from, layout.track.y, to - from, layout.track.h), color);
	};

	span(layout.start_x, layout.pre_end_x, pre_track_color);
	span(layout.pre_end_x, layout.end_x, final_color);

	if (data.source_fps) {
		render::rect_filled(
			gfx::Rect(layout.start_x - 1, layout.center_y - (SOURCE_MARK_HEIGHT / 2), 2, SOURCE_MARK_HEIGHT),
			gfx::Color(220, 220, 220, anim * 255)
		);
	}

	if (data.show_pre) {
		render::circle_filled({ layout.handover_x, layout.center_y }, THUMB_RADIUS, pre_track_color);

		if (layout.real_handover_x && std::abs(*layout.real_handover_x - layout.handover_x) > 2) {
			render::circle_stroke(
				{ *layout.real_handover_x, layout.center_y }, REAL_HANDOVER_RADIUS, pre_track_color, 2.f
			);
		}
	}

	render::circle_filled({ layout.end_x, layout.center_y }, THUMB_RADIUS, final_color);

	std::optional<gfx::Rect> edit_rect;
	if (editing != Thumb::NONE && helpers::text_input::has_text_edit(element.element->id))
		edit_rect = get_edit_rect(element, data, layout, editing);

	auto draw_value = [&](Thumb thumb, const gfx::Rect& value, const std::string& text, const gfx::Color& color) {
		if (edit_rect && editing == thumb) {
			auto text_input = data.text_input;
			text_input.font = data.font;
			render_label_edit(element, text_input, *edit_rect, color, anim);
		}
		else if (!edit_rect || !edit_rect->intersects(value)) {
			render::text(value.origin(), color, text, data.font);
		}
	};

	if (data.show_pre)
		draw_value(Thumb::PRE, layout.pre_value, handover_text(data), pre_color);
	draw_value(Thumb::FINAL, layout.final_value, final_text(data), text_color);
}

bool ui::update_interpolation_track(const Container& container, AnimatedElement& element) {
	auto& data = std::get<InterpolationTrackElementData>(element.element->data);
	auto& state = track_states[element.element->id];
	auto& hover_anim = element.animations.at(hasher("hover"));

	auto layout = get_layout(element, data);
	data.text_input.font = data.font;

	if (state.editing != Thumb::NONE) {
		bool editing = update_label_edit(
			container,
			element,
			data.text_input,
			get_edit_rect(element, data, layout, state.editing),
			[&](const std::string& text) {
				if (state.editing == Thumb::PRE)
					parse_handover(data, text);
				else
					parse_final(data, text);
			}
		);

		if (editing)
			return true;

		state.editing = Thumb::NONE;
	}

	if (state.dragging != Thumb::NONE) {
		// the press was claimed when the drag started, so it counts as held from then on
		if (get_active_element() == &element && keys::is_mouse_dragging()) {
			float position =
				static_cast<float>(keys::mouse_pos.x - layout.track.x) / static_cast<float>(layout.track.w);
			drag_to(data, state.dragging, snap(from_axis(position)));
			return true;
		}

		state.dragging = Thumb::NONE;
		hover_anim.set_goal(0.f);
		if (get_active_element() == &element)
			reset_active_element();
	}

	if (get_active_element())
		return false;

	bool pressed = keys::is_mouse_down();

	for (auto [thumb, value] :
	     { std::pair{ Thumb::PRE, layout.pre_value }, std::pair{ Thumb::FINAL, layout.final_value } })
	{
		if ((thumb == Thumb::PRE && !data.show_pre) || !value.contains(keys::mouse_pos) ||
		    !set_hovered_element(element))
			continue;

		set_cursor(SDL_SYSTEM_CURSOR_TEXT);
		if (!pressed)
			return false;

		keys::on_mouse_press_handled(SDL_BUTTON_LEFT);
		state.editing = thumb;

		std::string text = thumb == Thumb::PRE ? handover_text(data) : final_text(data);
		begin_label_edit(container, element, data.text_input, text == "-" ? "" : text);
		return true;
	}

	auto radius = static_cast<int>(THUMB_RADIUS);
	gfx::Rect track_area(
		layout.track.x - radius, layout.center_y - radius, layout.track.w + (radius * 2), (radius * 2) + 2
	);

	bool hovered = track_area.contains(keys::mouse_pos) && set_hovered_element(element);
	hover_anim.set_goal(hovered ? 1.f : 0.f);

	if (!hovered)
		return false;

	set_cursor(SDL_SYSTEM_CURSOR_POINTER);
	if (!pressed)
		return false;

	keys::on_mouse_press_handled(SDL_BUTTON_LEFT);
	set_active_element(element, "track");

	bool nearer_pre = std::abs(keys::mouse_pos.x - layout.handover_x) < std::abs(keys::mouse_pos.x - layout.end_x);
	state.dragging = data.show_pre && nearer_pre ? Thumb::PRE : Thumb::FINAL;

	float position = static_cast<float>(keys::mouse_pos.x - layout.track.x) / static_cast<float>(layout.track.w);
	drag_to(data, state.dragging, snap(from_axis(position)));
	return true;
}

std::string ui::get_interpolation_track_edit(const std::string& id) {
	auto it = track_states.find(id);
	if (it == track_states.end() || !helpers::text_input::has_text_edit(id))
		return "";

	switch (it->second.editing) {
		case Thumb::PRE:
			return "handover";
		case Thumb::FINAL:
			return "interpolated fps";
		case Thumb::NONE:
			return "";
	}

	return "";
}

void ui::remove_interpolation_track(AnimatedElement& element) {
	end_label_edit(element);
	track_states.erase(element.element->id);
}

ui::AnimatedElement* ui::add_interpolation_track(
	const std::string& id, Container& container, InterpolationTrackElementData data
) {
	int height = data.font.height() + VALUE_GAP + static_cast<int>(THUMB_RADIUS * 2);

	Element element(
		id,
		ElementType::INTERPOLATION_TRACK,
		gfx::Rect(container.current_position, gfx::Size(container.get_usable_rect().w, height)),
		std::move(data),
		render_interpolation_track,
		update_interpolation_track,
		remove_interpolation_track
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
