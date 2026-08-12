#include "../ui.h"
#include "../../render/render.h"

constexpr float GRAPH_ASPECT_RATIO = 16.f / 9.f;
constexpr gfx::Size GRAPH_SIZE = gfx::Size(300, (300 * (1 / GRAPH_ASPECT_RATIO)));
constexpr int LABEL_PADDING = 5;

constexpr float POINT_ANIMATION_SPEED = 25.f;

namespace {
	ui::AnimationState& get_point_animation(ui::AnimatedElement& element, size_t index, float start_value) {
		auto key = ui::hasher(std::format("point {}", index));
		auto [it, inserted] = element.animations.try_emplace(key, POINT_ANIMATION_SPEED, start_value);
		return it->second;
	}

	float get_point_animation_value(const ui::AnimatedElement& element, size_t index) {
		auto key = ui::hasher(std::format("point {}", index));
		auto it = element.animations.find(key);
		if (it != element.animations.end()) {
			return it->second.current;
		}
		return 0.0f;
	}

	void update_points_animation(ui::AnimatedElement& element) {
		const auto& graph_data = std::get<ui::WeightingGraphElementData>(element.element->data);

		if (graph_data.weights.empty())
			return;

		double max_weight = *std::ranges::max_element(graph_data.weights);
		if (max_weight == 0.0)
			return;

		int count = static_cast<int>(graph_data.weights.size());
		if (count < 2)
			return;

		for (size_t i = 0; i < graph_data.weights.size(); i++) {
			float norm_height = graph_data.weights[i] / max_weight;
			auto& point_anim = get_point_animation(element, i, norm_height);
			point_anim.set_goal(norm_height);
		}
	}
}

void ui::render_weighting_graph(const Container& container, const AnimatedElement& element) {
	const auto& graph_data = std::get<WeightingGraphElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	const gfx::Rect& rect = element.element->rect;

	size_t count = graph_data.weights.size();

	if (count < 2) {
		render::text(
			rect.center(),
			gfx::Color(180, 180, 180, anim * 255),
			"no blur frames",
			fonts::dejavu,
			FONT_CENTERED_X | FONT_CENTERED_Y
		);

		return;
	}

	gfx::Rect graph_rect = rect;

	float step_x = static_cast<float>(graph_rect.w) / (count - 1);

	std::vector<gfx::Point> points;
	points.reserve(count);

	for (int i = 0; i < count; ++i) {
		int x = graph_rect.x + static_cast<int>(i * step_x);

		if (i > 0 && x == points.back().x) // LOTS of points, don't render unneccessary ones
			continue;

		float point_val = get_point_animation_value(element, i);
		int y = graph_rect.y + graph_rect.h - static_cast<int>(point_val * graph_rect.h);
		points.emplace_back(x, y);
	}

	// draw connections
	for (size_t i = 1; i < points.size(); i++) {
		render::line(points[i - 1], points[i], gfx::Color(100, 100, 100, anim * 255));
	}

	if (graph_data.accurate_fps) {
		// draw points
		for (const auto& point : points) {
			render::circle_filled(point, 1.5f, gfx::Color(255, 255, 255, anim * 255));
		}

		// draw frame lines
		gfx::Color grid_line_color(150, 150, 150, anim * 80);

		for (const auto& pt : points) {
			gfx::Point start{ pt.x, graph_rect.y };
			gfx::Point end{ pt.x, graph_rect.y2() };

			gfx::Rect line_rect(pt, end);
			line_rect.w = 1;

			gfx::Rect upper = line_rect;
			upper.h /= 3;

			render::rect_filled_gradient(
				upper, render::GradientDirection::GRADIENT_DOWN, { grid_line_color.adjust_alpha(0.3f), grid_line_color }
			);

			gfx::Rect lower = line_rect;
			lower.y = upper.y2();
			lower.h = line_rect.h - upper.h;

			render::rect_filled_gradient(
				lower, render::GradientDirection::GRADIENT_DOWN, { grid_line_color, grid_line_color.adjust_alpha(0.f) }
			);
		}
	}

	// render::rect_stroke(graph_rect, grid_line_color);

	gfx::Color label_color(180, 180, 180, anim * 255);

	auto label_rect = rect.shrink(LABEL_PADDING);

	// render::text(
	// 	graph_rect.top_left().offset_y(-LABEL_PADDING), label_color, "previous", fonts::dejavu, FONT_BOTTOM_ALIGN
	// );

	// render::text(
	// 	graph_rect.top_right().offset_y(-LABEL_PADDING),
	// 	label_color,
	// 	"current",
	// 	fonts::dejavu,
	// 	FONT_RIGHT_ALIGN | FONT_BOTTOM_ALIGN
	// );

	// render::text({ label_rect.x, label_rect.center().y }, label_color, "blur amount", fonts::dejavu, FONT_NONE, -90,
	// 0);

	render::text(
		{ graph_rect.center().x, graph_rect.y2() },
		label_color,
		graph_data.accurate_fps ? std::format("blur frame ({})", count) : "blur frames depend on input fps",
		fonts::dejavu,
		FONT_CENTERED_X | FONT_BOTTOM_ALIGN
	);

	// debug
	// render::rect_stroke(rect, gfx::Color(75, 75, 75, anim * 255));
}

ui::AnimatedElement* ui::add_weighting_graph(
	const std::string& id, Container& container, const std::vector<double>& weights, bool accurate_fps
) {
	Element element(
		id,
		ElementType::WEIGHTING_GRAPH,
		gfx::Rect(
			container.current_position, gfx::Size{ std::min(GRAPH_SIZE.w, container.get_usable_rect().w), GRAPH_SIZE.h }
		),
		WeightingGraphElementData{
			.weights = weights,
			.accurate_fps = accurate_fps,
		},
		render_weighting_graph
	);

	auto* animated_element = add_element(
		container,
		std::move(element),
		container.element_gap,
		{
			{ hasher("main"), AnimationState(25.f) },
		}
	);

	update_points_animation(*animated_element);

	return animated_element;
}
