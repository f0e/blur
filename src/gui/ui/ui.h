#pragma once

namespace ui {
	enum class ElementType : std::uint8_t {
		BAR,
		TEXT,
		IMAGE,
		BUTTON,
		NOTIFICATION
	};

	struct BarElementData {
		float percent_fill;
		gfx::Color background_color;
		gfx::Color fill_color;
		std::optional<std::string> bar_text;
		std::optional<gfx::Color> text_color;
		std::optional<const SkFont*> font;

		bool operator==(const BarElementData& other) const {
			return percent_fill == other.percent_fill && background_color == other.background_color &&
			       fill_color == other.fill_color && bar_text == other.bar_text && text_color == other.text_color;
			// Skip font pointer comparison
		}
	};

	struct TextElementData {
		std::vector<std::string> lines;
		gfx::Color color;
		SkFont font;
		os::TextAlign align;

		bool operator==(const TextElementData& other) const {
			return lines == other.lines && color == other.color && align == other.align;
			// Skip font comparison since it might not implement ==
		}
	};

	struct ImageElementData {
		std::string image_path;
		os::SurfaceRef image_surface;
		std::string image_id;

		bool operator==(const ImageElementData& other) const {
			return image_path == other.image_path && image_id == other.image_id;
			// Skip image_surface comparison since it's a reference-counted pointer
		}
	};

	struct ButtonElementData {
		std::string text;
		SkFont font;
		std::optional<std::function<void()>> on_press;

		bool operator==(const ButtonElementData& other) const {
			return text == other.text;
			// Skip font comparison since it might not implement ==
			// also onpress function
		}
	};

	struct NotificationElementData {
		std::vector<std::string> lines;
		SkFont font;
		int line_height;

		bool operator==(const NotificationElementData& other) const {
			return lines == other.lines && line_height == other.line_height;
			// Skip font comparison since it might not implement ==
		}
	};

	using ElementData =
		std::variant<BarElementData, TextElementData, ImageElementData, ButtonElementData, NotificationElementData>;

	struct AnimationState {
		float speed;
		float current = 0.0f;
		bool complete = false;
		bool rendered_complete = false;

		AnimationState(float speed) : speed(speed) {}

		// delete default constructor since we always need a duration
		AnimationState() = delete;

		void update(float delta_time, bool stale) {
			float goal = !stale ? 1.f : 0.f;
			current = std::clamp(std::lerp(current, goal, speed * delta_time), 0.f, 1.f);

			bool was_complete = complete;
			complete = abs(current - goal) < 0.001f;

			if (complete && !was_complete) {
				rendered_complete = false;
			}
		}
	};

	struct Element {
		ElementType type;
		gfx::Rect rect;
		ElementData data;
		std::function<void(os::Surface*, const Element*, float)> render_fn;
		bool fixed = false;
	};

	struct AnimatedElement {
		std::unique_ptr<Element> element;
		AnimationState animation = AnimationState(25.f);
	};

	struct Container {
		gfx::Rect rect;
		std::optional<gfx::Color> background_color;
		std::unordered_map<std::string, AnimatedElement> elements;
		std::vector<std::string> current_element_ids;

		int line_height = 15;
		gfx::Point current_position;
		bool updated = false;
		int last_margin_bottom = 0;
	};

	void render_bar(os::Surface* surface, const Element* element, float anim);
	void render_text(os::Surface* surface, const Element* element, float anim);
	void render_image(os::Surface* surface, const Element* element, float anim);
	void render_button(os::Surface* surface, const Element* element, float anim);
	void render_notification(os::Surface* surface, const Element* element, float anim);

	void reset_container(
		Container& container, const gfx::Rect& rect, const SkFont& font, std::optional<gfx::Color> background_color = {}
	);

	Element* add_element(
		Container& container, const std::string& id, Element&& _element, int margin_bottom, float animation_speed = 25.f
	);
	Element* add_element(Container& container, const std::string& id, Element&& _element, float animation_speed = 25.f);

	Element& add_bar(
		const std::string& id,
		Container& container,
		float percent_fill,
		gfx::Color background_color,
		gfx::Color fill_color,
		int bar_width,
		std::optional<std::string> bar_text = {},
		std::optional<gfx::Color> text_color = {},
		std::optional<const SkFont*> font = {}
	);

	Element& add_text(
		const std::string& id,
		Container& container,
		const std::string& text,
		gfx::Color color,
		const SkFont& font,
		os::TextAlign align = os::TextAlign::Left,
		int margin_bottom = 7
	);

	Element& add_text_fixed(
		const std::string& id,
		Container& container,
		const gfx::Point& position,
		const std::string& text,
		gfx::Color color,
		const SkFont& font,
		os::TextAlign align = os::TextAlign::Left
	);

	std::optional<Element*> add_image(
		const std::string& id,
		Container& container,
		const std::string& image_path,
		const gfx::Size& max_size,
		std::string image_id = ""
	); // use image_id to distinguish images that have the same filename and reload it (e.g. if its updated)

	Element& add_button(
		const std::string& id,
		Container& container,
		const std::string& text,
		const SkFont& font,
		std::optional<std::function<void()>> on_press = {}
	);

	Element& add_notification(const std::string& id, Container& container, const std::string& text, const SkFont& font);

	void center_elements_in_container(Container& container, bool horizontal = true, bool vertical = true);
	bool update_container(Container& container, float delta_time);
	void render_container(os::Surface* surface, Container& container);
}
