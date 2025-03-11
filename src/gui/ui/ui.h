#pragma once

namespace ui {
	enum class ElementType : std::uint8_t {
		BAR,
		TEXT,
		IMAGE,
		BUTTON,
		NOTIFICATION,
		SLIDER,
		TEXT_INPUT,
		CHECKBOX,
		DROPDOWN,
		SEPARATOR
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

	enum class TextStyle : std::uint8_t {
		NORMAL,
		DROPSHADOW,
		OUTLINE
	};

	struct TextElementData {
		std::vector<std::string> lines;
		gfx::Color color;
		SkFont font;
		os::TextAlign align;
		TextStyle style;

		bool operator==(const TextElementData& other) const {
			return lines == other.lines && color == other.color && align == other.align && style == other.style;
			// Skip font comparison since it might not implement ==
		}
	};

	struct ImageElementData {
		std::filesystem::path image_path;
		os::SurfaceRef image_surface;
		std::string image_id;
		gfx::Color image_color;

		bool operator==(const ImageElementData& other) const {
			return image_path == other.image_path && image_id == other.image_id && image_color == other.image_color;
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

	enum class NotificationType : uint8_t {
		INFO,
		SUCCESS,
		NOTIF_ERROR
	};

	struct NotificationElementData {
		std::vector<std::string> lines;
		NotificationType type;
		SkFont font;
		int line_height;

		bool operator==(const NotificationElementData& other) const {
			return lines == other.lines && type == other.type && line_height == other.line_height;
			// Skip font comparison since it might not implement ==
		}
	};

	struct SliderElementData {
		std::variant<int, float> min_value;
		std::variant<int, float> max_value;
		std::variant<int*, float*> current_value;
		std::string label_format;
		SkFont font;
		std::optional<std::function<void(const std::variant<int*, float*>&)>> on_change;
		float precision;
		std::string tooltip;

		bool operator==(const SliderElementData& other) const {
			return min_value == other.min_value && max_value == other.max_value &&
			       current_value == other.current_value && label_format == other.label_format &&
			       precision == other.precision && tooltip == other.tooltip;
			// Skip font comparison since it might not implement ==
		}
	};

	struct TextInputElementData {
		std::string* text;
		std::string placeholder;
		SkFont font;
		std::optional<std::function<void(const std::string&)>> on_change;

		bool operator==(const TextInputElementData& other) const {
			return text == other.text && placeholder == other.placeholder;
			// Skip font comparison since it might not implement ==
			// and on_change
		}
	};

	struct CheckboxElementData {
		std::string label;
		bool* checked;
		SkFont font;
		std::optional<std::function<void(bool)>> on_change;

		bool operator==(const CheckboxElementData& other) const {
			return label == other.label && checked == other.checked;
			// Skip font comparison since it might not implement ==
			// and on_change
		}
	};

	struct DropdownElementData {
		std::string label;
		std::vector<std::string> options;
		std::string* selected;
		SkFont font;
		std::optional<std::function<void(std::string*)>> on_change;

		bool operator==(const DropdownElementData& other) const {
			return label == other.label && options == other.options && selected == other.selected;
			// Skip font comparison since it might not implement ==
			// and on_change
		}
	};

	using ElementData = std::variant<
		BarElementData,
		TextElementData,
		ImageElementData,
		ButtonElementData,
		NotificationElementData,
		SliderElementData,
		TextInputElementData,
		CheckboxElementData,
		DropdownElementData>;

	struct AnimationState {
		float speed;
		float current = 0.f;
		float goal = 0.f;
		bool complete = false;

		AnimationState(float speed, float value) : speed(speed), current(value), goal(value) {}

		// delete default constructor since we always need a duration
		AnimationState() = delete;

		void set_goal(float goal) {
			this->goal = goal;
		}

		bool update(float delta_time) {
			float old_current = current;
			current = std::clamp(u::lerp(current, goal, speed * delta_time, 0.001f), 0.f, 1.f);

			complete = current == goal;

			return current != old_current;
		}
	};

	struct AnimationInitialisation {
		float speed;
		float value = 0.f;
	};

	struct AnimatedElement;

	struct Container;

	struct Element {
		std::string id;
		ElementType type;
		gfx::Rect rect;
		ElementData data;
		std::function<void(const Container&, os::Surface*, const AnimatedElement&)> render_fn;
		std::optional<std::function<bool(const Container&, AnimatedElement&)>> update_fn;
		bool fixed = false;
		gfx::Rect orig_rect;

		Element(
			std::string id,
			ElementType type,
			const gfx::Rect& rect,
			ElementData data,
			std::function<void(const Container&, os::Surface*, const AnimatedElement&)> render_fn,
			std::optional<std::function<bool(const Container&, AnimatedElement&)>> update_fn = std::nullopt,
			bool fixed = false
		)
			: id(std::move(id)), type(type), rect(rect), data(std::move(data)), render_fn(std::move(render_fn)),
			  update_fn(std::move(update_fn)), fixed(fixed), orig_rect(rect) {}
	};

	struct AnimatedElement {
		std::unique_ptr<Element> element;
		std::unordered_map<size_t, AnimationState> animations;
		int z_index = 0;
	};

	const inline AnimationInitialisation DEFAULT_ANIMATION = { .speed = 25.f };

	struct Container {
		gfx::Rect rect;
		std::optional<gfx::Color> background_color;
		std::unordered_map<std::string, AnimatedElement> elements;
		std::vector<std::string> current_element_ids;

		int line_height = 15;
		gfx::Point current_position;
		std::optional<gfx::Point> padding;
		bool updated = false;
		int last_margin_bottom = 0;

		float scroll_y = 0.f;
		float scroll_speed_y = 0.f;

		[[nodiscard]] gfx::Rect get_usable_rect() const {
			gfx::Rect usable = rect;
			if (padding) {
				usable.x += padding->x;
				usable.y += padding->x;
				usable.w -= padding->x * 2;
				usable.h -= padding->y * 2;
			}
			return usable;
		}
	};

	inline auto hasher = std::hash<std::string>{};

	inline AnimatedElement* active_element = nullptr;

	inline const auto HIGHLIGHT_COLOR = gfx::rgba(133, 24, 16, 255);
	inline const int TYPE_SWITCH_PADDING = 5;

	void render_bar(const Container& container, os::Surface* surface, const AnimatedElement& element);

	void render_text(const Container& container, os::Surface* surface, const AnimatedElement& element);

	void render_image(const Container& container, os::Surface* surface, const AnimatedElement& element);

	void render_button(const Container& container, os::Surface* surface, const AnimatedElement& element);
	bool update_button(const Container& container, AnimatedElement& element);

	void render_notification(const Container& container, os::Surface* surface, const AnimatedElement& element);

	void render_slider(const Container& container, os::Surface* surface, const AnimatedElement& element);
	bool update_slider(const Container& container, AnimatedElement& element);

	void render_text_input(const Container& container, os::Surface* surface, const AnimatedElement& element);
	bool update_text_input(const Container& container, AnimatedElement& element);

	void render_checkbox(const Container& container, os::Surface* surface, const AnimatedElement& element);
	bool update_checkbox(const Container& container, AnimatedElement& element);

	void render_dropdown(const Container& container, os::Surface* surface, const AnimatedElement& element);
	bool update_dropdown(const Container& container, AnimatedElement& element);

	void render_separator(const Container& container, os::Surface* surface, const AnimatedElement& element);

	void reset_container(
		Container& container,
		const gfx::Rect& rect,
		int line_height,
		const std::optional<gfx::Point>& padding = {},
		std::optional<gfx::Color> background_color = {}
	);

	Element* add_element(
		Container& container,
		Element&& _element,
		int margin_bottom,
		const std::unordered_map<size_t, AnimationInitialisation>& animations = { { hasher("main"),
	                                                                                DEFAULT_ANIMATION } }
	);
	Element* add_element(
		Container& container,
		Element&& _element,
		const std::unordered_map<size_t, AnimationInitialisation>& animations = { { hasher("main"),
	                                                                                DEFAULT_ANIMATION } }
	);

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
		TextStyle style = TextStyle::NORMAL
	);

	Element& add_text(
		const std::string& id,
		Container& container,
		std::vector<std::string> lines,
		gfx::Color color,
		const SkFont& font,
		os::TextAlign align = os::TextAlign::Left,
		TextStyle style = TextStyle::NORMAL
	);

	Element& add_text_fixed(
		const std::string& id,
		Container& container,
		const gfx::Point& position,
		const std::string& text,
		gfx::Color color,
		const SkFont& font,
		os::TextAlign align = os::TextAlign::Left,
		TextStyle style = TextStyle::NORMAL
	);

	Element& add_text_fixed(
		const std::string& id,
		Container& container,
		const gfx::Point& position,
		std::vector<std::string> lines,
		gfx::Color color,
		const SkFont& font,
		os::TextAlign align = os::TextAlign::Left,
		TextStyle style = TextStyle::NORMAL
	);

	std::optional<Element*> add_image(
		const std::string& id,
		Container& container,
		const std::filesystem::path& image_path,
		const gfx::Size& max_size,
		std::string image_id = "",
		gfx::Color image_color = gfx::rgba(255, 255, 255, 255)
	); // use image_id to distinguish images that have the same filename and reload it (e.g. if its updated)

	Element& add_button(
		const std::string& id,
		Container& container,
		const std::string& text,
		const SkFont& font,
		std::optional<std::function<void()>> on_press = {}
	);

	Element& add_notification(
		const std::string& id, Container& container, const std::string& text, NotificationType type, const SkFont& font
	);

	Element& add_slider(
		const std::string& id,
		Container& container,
		const std::variant<int, float>& min_value,
		const std::variant<int, float>& max_value,
		std::variant<int*, float*> value,
		const std::string& label_format,
		const SkFont& font,
		std::optional<std::function<void(const std::variant<int*, float*>&)>> on_change = {},
		float precision = 0.f,
		const std::string& tooltip = ""
	);

	Element& add_text_input(
		const std::string& id,
		Container& container,
		std::string& text,
		const std::string& placeholder,
		const SkFont& font,
		std::optional<std::function<void(const std::string&)>> on_change = {}
	);

	Element& add_checkbox(
		const std::string& id,
		Container& container,
		const std::string& label,
		bool& checked,
		const SkFont& font,
		std::optional<std::function<void(bool)>> on_change = {}
	);

	Element& add_dropdown(
		const std::string& id,
		Container& container,
		const std::string& label,
		const std::vector<std::string>& options,
		std::string& selected,
		const SkFont& font,
		std::optional<std::function<void(std::string*)>> on_change = {}
	);

	Element& add_separator(const std::string& id, Container& container);

	void add_spacing(Container& container, int spacing);

	void set_next_same_line(Container& container);

	void center_elements_in_container(Container& container, bool horizontal = true, bool vertical = true);

	std::vector<decltype(Container::elements)::iterator> get_sorted_container_elements(Container& container);

	void set_cursor(os::NativeCursor cursor);

	bool set_hovered_element(AnimatedElement& element);
	AnimatedElement* get_hovered_element();

	bool update_container_input(Container& container);
	void on_update_input_start();
	void on_update_input_end();

	bool update_container_frame(Container& container, float delta_time);
	void on_update_frame_end();

	void render_container(os::Surface* surface, Container& container);

	void on_frame_start();
}
