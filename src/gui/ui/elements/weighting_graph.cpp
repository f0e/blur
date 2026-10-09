#include "../ui.h"
#include "../../render/render.h"

constexpr int GRAPH_WIDTH = 300;
constexpr int LABEL_PADDING = 5;
constexpr int BARS_HEIGHT = 45;

// the timeline's scale fits at least this many seconds, so settings with the same exposure (like blur amount tied to
// fps) draw the same window and only the spacing between output frames changes
constexpr float MIN_TIMELINE_SPAN = 0.1f;

// ticks under the graph between each output frame, so a changing scale reads as zooming rather than the windows
// changing length
constexpr int TICK_HEIGHT = 5;

// columns at least this wide get a pixel gap between bars, narrower ones are drawn edge to edge
constexpr float MIN_GAPPED_COLUMN_WIDTH = 3.f;

// resolution the window's shape is resampled to, so shapes with different frame counts can be blended between
constexpr int SHAPE_SAMPLES = 257;

constexpr float TRANSITION_SPEED = 25.f;

namespace {
	// everything the graph draws, in pixels from the current output frame, so it can be blended between settings and
	// the bars slide and resize rather than jump. blending in pixels rather than seconds keeps what's on screen moving
	// in a straight line when the scale changes a lot
	struct Geometry {
		float window_start = 0.f; // first and last blended frame
		float window_end = 0.f;
		float frame_period = 0.f;   // between source frames
		float frame_interval = 0.f; // between output frames
		float span = 0.f;           // width of the timeline
		std::vector<float> shape;   // heights across the window, the tallest is 1

		bool operator==(const Geometry& other) const = default;

		[[nodiscard]] Geometry lerp(const Geometry& to, float t) const {
			Geometry result{
				.window_start = std::lerp(window_start, to.window_start, t),
				.window_end = std::lerp(window_end, to.window_end, t),
				.frame_period = std::lerp(frame_period, to.frame_period, t),
				.frame_interval = std::lerp(frame_interval, to.frame_interval, t),
				.span = std::lerp(span, to.span, t),
				.shape = shape,
			};

			for (size_t i = 0; i < result.shape.size(); i++)
				result.shape[i] = std::lerp(shape[i], to.shape[i], t);

			return result;
		}

		// height at a point across the window, from 0 to 1
		[[nodiscard]] float height_at(float position) const {
			float sample = std::clamp(position, 0.f, 1.f) * (SHAPE_SAMPLES - 1);
			int first = static_cast<int>(sample);
			int second = std::min(first + 1, SHAPE_SAMPLES - 1);
			return std::lerp(shape[first], shape[second], sample - static_cast<float>(first));
		}
	};

	// blending from what was drawn when the settings last changed to where they are now
	struct GraphState {
		Geometry from;
		Geometry to;
	};

	std::unordered_map<std::string, GraphState> graph_states;

	// the current window's bars point up from a baseline and with a known frame gap the previous and next windows'
	// point down from it, or once those overlap each other the three get a row each in the same space
	int get_graph_height(double frame_gap) {
		int ruler_height = frame_gap > 0 ? TICK_HEIGHT + LABEL_PADDING : 0;
		return BARS_HEIGHT + (frame_gap > 0 ? BARS_HEIGHT + 1 : 0) + LABEL_PADDING + ruler_height +
		       fonts::dejavu.height();
	}

	Geometry get_geometry(const ui::WeightingGraphElementData& graph_data, int width) {
		int count = static_cast<int>(graph_data.weights.size());
		float frame_period = 1.f / static_cast<float>(std::max(graph_data.source_fps, 1));

		float half = static_cast<float>(count - 1) / 2;
		auto center = static_cast<float>(graph_data.window_center);
		auto gap = static_cast<float>(graph_data.frame_gap);

		// the scale fits every visible window, or MIN_TIMELINE_SPAN if that's wider. the timeline is then cut to the
		// most output frames that fit (an odd number so both ends land on boundaries between them), so dragging fps
		// moves the ends rather than zooming. in source frame columns
		auto columns = static_cast<float>(count);
		auto span_columns = columns;
		if (gap > 0) {
			float needed = 2 * (std::abs(center) + half + 0.5f + gap);
			columns = std::max(needed, MIN_TIMELINE_SPAN / frame_period);
			span_columns = ((2 * std::floor(((columns / gap) - 1) / 2)) + 1) * gap;

			if (span_columns < needed) {
				span_columns = ((2 * std::ceil(((needed / gap) - 1) / 2)) + 1) * gap;
				columns = span_columns;
			}
		}

		// whole pixel columns when there's a gap between bars so every bar and gap is the same size
		float column_width = static_cast<float>(width) / columns;
		if (column_width >= MIN_GAPPED_COLUMN_WIDTH)
			column_width = std::floor(column_width);

		Geometry geometry{
			.window_start = (center - half) * column_width,
			.window_end = (center + half) * column_width,
			.frame_period = column_width,
			.frame_interval = gap * column_width,
			.span = span_columns * column_width,
			.shape = std::vector<float>(SHAPE_SAMPLES),
		};

		double max_weight = *std::ranges::max_element(graph_data.weights);
		for (int sample = 0; sample < SHAPE_SAMPLES; sample++) {
			double index = static_cast<double>(sample) / (SHAPE_SAMPLES - 1) * (count - 1);
			int first = static_cast<int>(index);
			int second = std::min(first + 1, count - 1);
			geometry.shape[sample] = static_cast<float>(
				std::lerp(graph_data.weights[first], graph_data.weights[second], index - first) / max_weight
			);
		}

		return geometry;
	}

	void update_geometry(ui::AnimatedElement& element) {
		const auto& graph_data = std::get<ui::WeightingGraphElementData>(element.element->data);

		if (graph_data.weights.size() < 2 || *std::ranges::max_element(graph_data.weights) <= 0.0)
			return;

		auto target = get_geometry(graph_data, element.element->rect.w);
		auto& transition = element.animations.at(ui::hasher("transition"));

		auto [it, inserted] = graph_states.try_emplace(element.element->id, GraphState{ .from = target, .to = target });
		auto& state = it->second;

		if (!inserted && state.to != target) {
			// start from what's on screen so changing the settings mid-transition stays smooth
			state.from = state.from.lerp(state.to, transition.current);
			state.to = target;
			transition.current = 0.f;
			transition.set_goal(1.f);
		}
	}

	void remove_graph(ui::AnimatedElement& element) {
		graph_states.erase(element.element->id);
	}
}

void ui::render_weighting_graph(const Container& container, const AnimatedElement& element) {
	const auto& graph_data = std::get<WeightingGraphElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	const gfx::Rect& rect = element.element->rect;

	int count = static_cast<int>(graph_data.weights.size());
	double frame_gap = graph_data.frame_gap;

	gfx::Color label_color(180, 180, 180, anim * 255);

	auto state_it = graph_states.find(element.element->id);
	if (count < 2 || state_it == graph_states.end()) {
		render::text(rect.center(), label_color, "no blur frames", fonts::dejavu, FONT_CENTERED_X | FONT_CENTERED_Y);

		return;
	}

	const auto& state = state_it->second;
	Geometry geometry = state.from.lerp(state.to, element.animations.at(hasher("transition")).current);

	// each bar is a source frame on a timeline shared by the previous, current and next output frames, with the
	// current output frame in the middle
	bool show_neighbours = frame_gap > 0;
	int bar_gap = geometry.frame_period >= MIN_GAPPED_COLUMN_WIDTH ? 1 : 0;

	auto to_x = [&](float offset) {
		return static_cast<int>(std::lround(static_cast<float>(rect.center().x) + offset));
	};

	gfx::Color current_color(230, 230, 230, anim * 255);
	gfx::Color neighbour_color(90, 90, 90, anim * 255);

	// a window's bars growing up (or down) from baseline. each bar runs to where the next one starts so bars stay edge
	// to edge (or a pixel apart) at any scale, and the whole window is shifted by one rounded amount so its bars keep
	// their spacing while it slides
	auto draw_window = [&](int window, int baseline, int max_height, bool down, const gfx::Color& color) {
		int shift = static_cast<int>(std::lround(static_cast<float>(window) * geometry.frame_interval));
		float window_length = geometry.window_end - geometry.window_start;

		int first_frame = static_cast<int>(std::ceil((geometry.window_start / geometry.frame_period) - 0.001f));
		int last_frame = static_cast<int>(std::floor((geometry.window_end / geometry.frame_period) + 0.001f));

		for (int frame = first_frame; frame <= last_frame; frame++) {
			float offset = static_cast<float>(frame) * geometry.frame_period;
			float position = window_length > 0.f ? (offset - geometry.window_start) / window_length : 0.5f;
			int height = static_cast<int>(std::lround(geometry.height_at(position) * static_cast<float>(max_height)));

			int x = to_x(offset - (geometry.frame_period / 2)) + shift;
			int width = std::max(to_x(offset + (geometry.frame_period / 2)) + shift - bar_gap - x, 1);
			if (height <= 0 || x < rect.x || x + width > rect.x2())
				continue;

			render::rect_filled(gfx::Rect(x, down ? baseline : baseline - height, width, height), color);
		}
	};

	// the previous and next windows only overlap each other above blur amount 2
	bool neighbours_overlap = count - 1 > 2 * frame_gap;

	if (!show_neighbours) {
		draw_window(0, rect.y + BARS_HEIGHT, BARS_HEIGHT, false, current_color);
	}
	else if (!neighbours_overlap) {
		int baseline = rect.y + BARS_HEIGHT;
		draw_window(-1, baseline + 1, BARS_HEIGHT, true, neighbour_color);
		draw_window(1, baseline + 1, BARS_HEIGHT, true, neighbour_color);
		draw_window(0, baseline, BARS_HEIGHT, false, current_color);
	}
	else {
		int row_height = ((BARS_HEIGHT * 2) + 1) / 3;
		for (int row = 0; row < 3; row++) {
			int window = row - 1;
			draw_window(
				window,
				rect.y + ((row + 1) * row_height),
				row_height - 3,
				false,
				window == 0 ? current_color : neighbour_color
			);
		}
	}

	if (show_neighbours && geometry.frame_interval > 0.f) {
		int tick_y = rect.y2() - fonts::dejavu.height() - LABEL_PADDING - TICK_HEIGHT;
		gfx::Color tick_color(130, 130, 130, anim * 255);

		int boundaries = static_cast<int>((geometry.span / 2 / geometry.frame_interval) + 0.51f);
		for (int boundary = 0; boundary < boundaries; boundary++) {
			float offset = (static_cast<float>(boundary) + 0.5f) * geometry.frame_interval;
			render::rect_filled(gfx::Rect(to_x(-offset), tick_y, 1, TICK_HEIGHT), tick_color);
			render::rect_filled(gfx::Rect(to_x(offset), tick_y, 1, TICK_HEIGHT), tick_color);
		}
	}

	// frames with no weight aren't blended
	auto blended_frames = std::ranges::count_if(graph_data.weights, [](double weight) {
		return weight > 0.0;
	});

	render::text(
		{ rect.center().x, rect.y2() },
		label_color,
		graph_data.fps_known ? std::format("{} blur frames", blended_frames) : "blur frames depend on input fps",
		fonts::dejavu,
		FONT_CENTERED_X | FONT_BOTTOM_ALIGN
	);
}

ui::AnimatedElement* ui::add_weighting_graph(
	const std::string& id,
	Container& container,
	const std::vector<double>& weights,
	double window_center,
	double frame_gap,
	int source_fps,
	bool fps_known
) {
	Element element(
		id,
		ElementType::WEIGHTING_GRAPH,
		gfx::Rect(
			container.current_position,
			gfx::Size{
				std::min(GRAPH_WIDTH, container.get_usable_rect().w),
				get_graph_height(frame_gap),
			}
		),
		WeightingGraphElementData{
			.weights = weights,
			.window_center = window_center,
			.frame_gap = frame_gap,
			.source_fps = source_fps,
			.fps_known = fps_known,
		},
		render_weighting_graph,
		std::nullopt,
		remove_graph
	);

	auto* animated_element = add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(25.f) },
			{ hasher("transition"), AnimationState(TRANSITION_SPEED, 1.f) },
		}
	);

	update_geometry(*animated_element);

	return animated_element;
}
