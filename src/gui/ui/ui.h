#pragma once

#include "gfx/point.h"
#include "gfx/size.h"
#include "gfx/color.h"
#include "os/draw_text.h"
#include "os/font.h"

namespace ui {
	enum class ElementType {
		BAR,
		TEXT,
		IMAGE
	};

	struct BarElementData {
		float percent_fill;
		gfx::Color background_color;
		gfx::Color fill_color;
	};

	struct TextElementData {
		std::string text;
		gfx::Color color;
		SkFont font;
		os::TextAlign align;
	};

	struct ImageElementData {
		os::SurfaceRef image_surface;
	};

	using ElementData = std::variant<BarElementData, TextElementData, ImageElementData>;

	struct AnimationState {
		float speed;
		float current = 0.0f;

		AnimationState(float _speed)
			: speed(_speed) {}

		// delete default constructor since we always need a duration
		AnimationState() = delete;

		bool update(float delta_time, bool stale) {
			float goal = !stale ? 1.f : 0.f;

			current = std::lerp(current, goal, speed * delta_time);

			return abs(current - goal) < 0.01f;
		}
	};

	struct Element {
		ElementType type;
		gfx::Rect rect;
		ElementData data;
		std::function<void(os::Surface*, const Element*, float)> render_fn;
	};

	struct AnimatedElement {
		std::shared_ptr<Element> element;
		AnimationState animation = AnimationState(25.f);
	};

	struct Container {
		gfx::Rect rect;
		std::optional<gfx::Color> background_color;
		std::unordered_map<std::string, AnimatedElement> elements;
		std::vector<std::string> current_element_ids;

		gfx::Point current_position;
	};

	void render_bar(os::Surface* surface, const Element* element, float anim);
	void render_text(os::Surface* surface, const Element* element, float anim);
	void render_image(os::Surface* surface, const Element* element, float anim);

	void init_container(Container& container, gfx::Rect rect, std::optional<gfx::Color> background_color = {});
	void add_element(Container& container, const std::string& id, const Element& element);

	void add_bar(const std::string& id, Container& container, float percent_fill, gfx::Color background_color, gfx::Color fill_color);
	void add_text(const std::string& id, Container& container, const std::string& text, gfx::Color color, const SkFont& font, os::TextAlign align = os::TextAlign::Left);
	void add_image(const std::string& id, Container& container, std::string image_path, gfx::Size max_size);

	void center_elements_in_container(Container& container, bool horizontal = true, bool vertical = true);
	void render_container(os::Surface* surface, Container& container, float delta_time);
}
