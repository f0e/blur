#pragma once

#include "../render/render.h"
#include "helpers/text_input.h"
#include "helpers/video.h"

namespace ui {
	inline constexpr gfx::Color DEFAULT_HIGHLIGHT_COLOR = { 133, 24, 16, 255 };
	inline gfx::Color highlight_color = DEFAULT_HIGHLIGHT_COLOR;

	inline size_t frame = 0;

	struct Padding {
		int top = 0;
		int right = 0;
		int bottom = 0;
		int left = 0;

		Padding(int top, int right, int bottom, int left) : top(top), right(right), bottom(bottom), left(left) {}

		Padding(int vertical, int horizontal) : top(vertical), right(horizontal), bottom(vertical), left(horizontal) {}

		Padding(int all) : top(all), right(all), bottom(all), left(all) {}
	};

	enum class ElementType : std::uint8_t {
		BAR,
		TEXT,
		IMAGE,
		VIDEO,
		BUTTON,
		ICON_BUTTON,
		NOTIFICATION,
		SLIDER,
		TEXT_INPUT,
		CHECKBOX,
		DROPDOWN,
		COLOR_PICKER,
		SEEK_BAR,
		SEPARATOR,
		WEIGHTING_GRAPH,
		TABS,
		HINT,
		SPINNER,
		UPDATE_NOTICE,
		LOGO_AND_VERSION,
		LINK
	};

	struct BarElementData {
		float percent_fill;
		gfx::Color background_color;
		gfx::Color fill_color;
		std::optional<std::string> bar_text;
		std::optional<gfx::Color> text_color;
		std::optional<render::Font> font;

		bool operator==(const BarElementData& other) const {
			return percent_fill == other.percent_fill && background_color == other.background_color &&
			       fill_color == other.fill_color && bar_text == other.bar_text && text_color == other.text_color &&
			       font == other.font;
		}
	};

	struct TextElementData {
		std::vector<std::string> lines;
		gfx::Color color;
		render::Font font;
		unsigned int flags;

		bool operator==(const TextElementData& other) const {
			return lines == other.lines && color == other.color && font == other.font && flags == other.flags;
		}
	};

	enum class SeparatorStyle : std::uint8_t {
		NORMAL,
		FADE_RIGHT,
		FADE_LEFT,
		FADE_BOTH
	};

	struct SeparatorElementData {
		SeparatorStyle style;

		bool operator==(const SeparatorElementData& other) const {
			return style == other.style;
		}
	};

	namespace texture_cache {
		struct TextureWrapper {
			size_t last_access_frame;
			std::shared_ptr<render::Texture> texture;
		};

		inline std::unordered_map<std::string, TextureWrapper> map;

		inline std::shared_ptr<render::Texture> get_or_load_texture(
			const std::filesystem::path& path, const std::string& id
		) {
			// Use a combined key of path and id for caching
			std::string cache_key = path.string() + "_" + id;

			auto it = map.find(cache_key);
			if (it != map.end()) {
				auto& item = it->second;
				item.last_access_frame = frame;
				return item.texture;
			}

			// Load new texture
			auto texture = std::make_shared<render::Texture>();
			if (texture->load_from_file(path.string())) {
				map[cache_key] = {
					.last_access_frame = frame,
					.texture = texture,
				};
				return texture;
			}

			return nullptr;
		}

		inline void remove_old() {
			std::erase_if(texture_cache::map, [](const auto& pair) {
				return pair.second.last_access_frame != frame;
			});
		}
	}

	struct ImageElementData {
		std::shared_ptr<render::Texture> texture;
		std::string image_id;
		gfx::Color image_color;

		bool operator==(const ImageElementData& other) const {
			return texture == other.texture && image_id == other.image_id && image_color == other.image_color;
		}
	};

	struct UIVideo {
		size_t video_id;
		std::filesystem::path path;
		std::optional<u::VideoInfo> video_info;

		bool operator==(const UIVideo& other) const = default;
	};

	struct VideoElementData {
		struct StoredWaveform {
			std::vector<int16_t> samples;
			int16_t max_sample = 0;
		};

		struct Video {
			UIVideo data;
			gfx::Size size;
			std::optional<StoredWaveform*> waveform;
			std::optional<gui_utils::ThumbnailRes> thumbnail;

			bool operator==(const Video& other) const {
				return data == other.data && size == other.size && waveform == other.waveform &&
				       thumbnail == other.thumbnail;
			}
		};

		struct TrimHandleInfo {
			bool grabbing;
			bool grab_moving = false;
			std::optional<int> grab_start_mouse_x;
			// for the future
		};

		std::vector<Video> videos;
		size_t* index;
		float* start;
		float* end;
		bool trim_disabled = false;
		std::optional<float> saved_percent;
		TrimHandleInfo handle_info;
		std::function<void(size_t video_id)> on_remove;

		// persistent
		std::optional<std::filesystem::path> last_active_video;
		std::optional<int> last_pan_x;

		bool operator==(const VideoElementData& other) const {
			return videos == other.videos && index == other.index && start == other.start && end == other.end;
		}
	};

	struct ButtonElementData {
		std::string text;
		render::Font font;
		std::optional<std::function<void()>> on_press;
		std::optional<gfx::Color> accent_color;

		bool operator==(const ButtonElementData& other) const {
			return text == other.text && font == other.font && accent_color == other.accent_color;
		}
	};

	struct IconButtonElementData {
		std::string icon;
		render::Font font;
		gfx::Color color;
		gfx::Color hover_color;
		std::string tooltip;
		std::optional<std::function<void()>> on_press;

		bool operator==(const IconButtonElementData& other) const {
			return icon == other.icon && font == other.font && color == other.color &&
			       hover_color == other.hover_color && tooltip == other.tooltip;
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
		render::Font font;
		int line_height;
		std::optional<std::function<void(const std::string& id)>> on_click;
		std::optional<std::function<void(const std::string& id)>> on_close;

		bool operator==(const NotificationElementData& other) const {
			return lines == other.lines && type == other.type && font == other.font && line_height == other.line_height;
		}
	};

	struct SliderElementData {
		std::variant<int, float> min_value;
		std::variant<int, float> max_value;
		std::variant<int*, float*> current_value;
		std::string label_format;
		render::Font font;
		std::optional<std::function<void(const std::variant<int*, float*>&)>> on_change;
		float precision;
		std::string tooltip;
		bool is_tied_slider;
		bool* is_tied;
		std::optional<std::variant<int*, float*>> tied_value;
		std::string tied_text;

		std::string editing_text;
		helpers::text_input::TextInputData text_input;

		bool operator==(const SliderElementData& other) const {
			return min_value == other.min_value && max_value == other.max_value &&
			       current_value == other.current_value && label_format == other.label_format && font == other.font &&
			       precision == other.precision && tooltip == other.tooltip && is_tied_slider == other.is_tied_slider &&
			       is_tied == other.is_tied && tied_value == other.tied_value && tied_text == other.tied_text;
			// && text_input == other.text_input;
		}
	};

	struct TextInputElementData {
		helpers::text_input::TextInputData text_input;
		std::string label;
		std::string placeholder;

		bool operator==(const TextInputElementData& other) const {
			return text_input == other.text_input && label == other.label && placeholder == other.placeholder;
		}
	};

	struct SeekBarElementData {
		float* value;
		float duration;
		render::Font font;

		bool operator==(const SeekBarElementData& other) const {
			return value == other.value && duration == other.duration && font == other.font;
		}
	};

	struct CheckboxElementData {
		std::string label;
		bool* checked;
		render::Font font;
		std::optional<std::function<void(bool)>> on_change;

		bool operator==(const CheckboxElementData& other) const {
			return label == other.label && checked == other.checked && font == other.font;
		}
	};

	struct DropdownElementData {
		std::string label;
		std::vector<std::string> options;
		std::string* selected;
		render::Font font;
		std::optional<std::function<void(std::string*)>> on_change;
		std::vector<std::string> muted_options;

		std::string hovered_option;

		bool operator==(const DropdownElementData& other) const {
			return label == other.label && options == other.options && selected == other.selected &&
			       font == other.font && muted_options == other.muted_options;
		}
	};

	struct ColorPickerElementData {
		std::string label;
		std::string* hex; // empty = use the default color
		render::Font font;
		gfx::Color default_color;
		std::vector<gfx::Color> presets;
		std::optional<std::function<void()>> on_change;

		// internal state. kept out of operator== so it survives the element being rebuilt each frame
		float hue = 0.f;
		float saturation = 0.f;
		float brightness = 0.f;
		std::string synced_hex; // the hex the hsb above was taken from, to notice outside changes
		int drag_target = 0;    // ColorPickerDrag
		bool mouse_was_down = false;
		bool open = false;
		int hovered_preset = -1;

		std::string editing_text; // scratch buffer while the hex is being typed into, so half-typed values
		                          // don't get written out as the color
		helpers::text_input::TextInputData text_input;

		bool operator==(const ColorPickerElementData& other) const {
			return label == other.label && hex == other.hex && font == other.font &&
			       default_color == other.default_color && presets == other.presets;
		}
	};

	struct WeightingGraphElementData {
		std::vector<double> weights;
		bool accurate_fps;

		bool operator==(const WeightingGraphElementData& other) const {
			return weights == other.weights && accurate_fps == other.accurate_fps;
		}
	};

	struct TabsElementData {
		std::vector<std::string> options;

		std::string* selected;
		render::Font font;
		std::optional<std::function<void()>> on_select;

		std::vector<gfx::Rect> option_offset_rects;

		bool operator==(const TabsElementData& other) const {
			return options == other.options && selected == other.selected && font == other.font;
		}
	};

	struct Paragraph {
		std::vector<std::string> lines;
		bool is_last = false;

		bool operator==(const Paragraph& other) const {
			return lines == other.lines && is_last == other.is_last;
		}
	};

	struct HintElementData {
		std::vector<Paragraph> paragraphs;
		gfx::Color color;
		render::Font font;

		bool operator==(const HintElementData& other) const {
			return paragraphs == other.paragraphs && color == other.color && font == other.font;
		}
	};

	struct SpinnerElementData {
		gfx::Color background_color;
		gfx::Color highlight_color;
		float radius;
		float thickness;
		float trail_degrees;

		bool operator==(const SpinnerElementData& other) const {
			return background_color == other.background_color && highlight_color == other.highlight_color &&
			       radius == other.radius && thickness == other.thickness && trail_degrees == other.trail_degrees;
		}
	};

	struct UpdateNoticeLink {
		std::string text;
		bool primary = false;
		std::optional<std::function<void()>> on_press;

		bool operator==(const UpdateNoticeLink& other) const {
			return text == other.text && primary == other.primary;
		}
	};

	enum class UpdateNoticeAlign : std::uint8_t {
		RIGHT,
		CENTER,
	};

	struct UpdateNoticeElementData {
		std::string status;
		std::string subtext;
		std::optional<float> progress;
		UpdateNoticeAlign align;
		render::Font font;

		bool operator==(const UpdateNoticeElementData& other) const {
			return status == other.status && subtext == other.subtext && progress == other.progress &&
			       align == other.align && font == other.font;
		}
	};

	struct LogoAndVersionElementData {
		std::shared_ptr<render::Texture> logo;
		std::string title;
		std::string subtitle;
		render::Font title_font;
		render::Font font;

		bool operator==(const LogoAndVersionElementData& other) const {
			return logo == other.logo && title == other.title && subtitle == other.subtitle &&
			       title_font == other.title_font && font == other.font;
		}
	};

	struct LinkElementData {
		std::string text;
		std::optional<std::function<void()>> on_press;
		gfx::Color color;
		gfx::Color hover_color;
		render::Font font;

		bool operator==(const LinkElementData& other) const {
			return text == other.text && color == other.color && hover_color == other.hover_color && font == other.font;
		}
	};

	using ElementData = std::variant<
		BarElementData,
		TextElementData,
		ImageElementData,
		VideoElementData,
		ButtonElementData,
		IconButtonElementData,
		NotificationElementData,
		SliderElementData,
		SeekBarElementData,
		TextInputElementData,
		CheckboxElementData,
		DropdownElementData,
		ColorPickerElementData,
		SeparatorElementData,
		WeightingGraphElementData,
		TabsElementData,
		HintElementData,
		SpinnerElementData,
		UpdateNoticeElementData,
		LogoAndVersionElementData,
		LinkElementData>;

	struct AnimationState {
		float speed;
		float current = 0.f;
		float goal = 0.f;
		float snap = 0.001f;
		bool complete = false;

		AnimationState(float speed, float value = 0.f, float snap = 0.001f)
			: speed(speed), current(value), goal(value), snap(snap) {}

		// delete default constructor since we always need a duration
		AnimationState() = delete;

		void set_goal(float goal) {
			this->goal = goal;
		}

		bool update(float delta_time) {
			float old_current = current;
			current = std::clamp(
				u::lerp(current, goal, speed * delta_time, snap), std::min(current, goal), std::max(current, goal)
			);

			complete = current == goal;

			return current != old_current;
		}
	};

	struct AnimatedElement;

	struct Container;

	struct Element {
		std::string id;
		ElementType type;
		gfx::Rect rect;
		ElementData data;
		std::function<void(const Container&, const AnimatedElement&)> render_fn;
		std::optional<std::function<bool(const Container&, AnimatedElement&)>> update_fn;
		std::optional<std::function<void(AnimatedElement&)>> remove_fn;
		std::optional<std::function<void(AnimatedElement&)>> stale_fn;
		bool fixed = false;
		bool always_render = false;
		gfx::Rect orig_rect;

		Element(
			std::string id,
			ElementType type,
			const gfx::Rect& rect,
			ElementData data,
			std::function<void(const Container&, const AnimatedElement&)> render_fn,
			std::optional<std::function<bool(const Container&, AnimatedElement&)>> update_fn = std::nullopt,
			std::optional<std::function<void(AnimatedElement&)>> remove_fn = std::nullopt,
			std::optional<std::function<void(AnimatedElement&)>> stale_fn = std::nullopt,
			bool fixed = false,
			bool always_render = false
		)
			: id(std::move(id)), type(type), rect(rect), data(std::move(data)), render_fn(std::move(render_fn)),
			  update_fn(std::move(update_fn)), remove_fn(std::move(remove_fn)), stale_fn(std::move(stale_fn)),
			  fixed(fixed), always_render(always_render), orig_rect(rect) {}

		bool update(const Element& other) {
			this->id = other.id;
			this->type = other.type;
			this->rect = other.rect;
			this->render_fn = other.render_fn;
			this->update_fn = other.update_fn;
			this->remove_fn = other.remove_fn;
			this->stale_fn = other.stale_fn;
			this->fixed = other.fixed;
			this->always_render = other.always_render;
			this->orig_rect = other.orig_rect;

			bool updated = this->data != other.data;
			if (updated) {
				this->data = other.data;
			}
			return updated;
		}
	};

	struct AnimatedElement {
		std::unique_ptr<Element> element;
		std::unordered_map<size_t, AnimationState> animations;
		int z_index = 0;
		bool went_stale = false;
	};

	const inline AnimationState DEFAULT_ANIMATION(25.f);

	struct Container {
		SDL_Window* window;

		gfx::Rect rect;
		std::optional<gfx::Color> background_color;
		std::unordered_map<std::string, AnimatedElement> elements;
		std::vector<std::string> current_element_ids;

		int element_gap = 15;
		float line_height = 1.2f;

		gfx::Point current_position;
		std::optional<Padding> padding;
		bool updated = false;
		int last_margin_bottom = 0;

		bool next_same_line = false; // set by set_next_same_line, consumed by the next added element
		int same_line_bottom = 0;    // where the line the next element joins ends, so shorter elements don't pull the
		                             // following ones up into it

		float scroll_y = 0.f;
		float scroll_speed_y = 0.f;
		bool scroll_to_top = false;

		AnimationState scrollbar_anim = AnimationState(60.f);

		[[nodiscard]] gfx::Rect get_usable_rect() const {
			gfx::Rect usable = rect;
			if (padding) {
				usable.x += padding->left;
				usable.y += padding->top;
				usable.w -= padding->left + padding->right;
				usable.h -= padding->top + padding->bottom;
			}
			return usable;
		}

		std::stack<int> element_gaps;

		void push_element_gap(int new_element_gap) {
			element_gaps.push(element_gap);
			element_gap = new_element_gap;
		}

		void pop_element_gap() {
			int new_element_gap = element_gaps.top();
			element_gap = new_element_gap;
			element_gaps.pop();
		}
	};

	inline auto hasher = std::hash<std::string>{};

	inline std::vector<SDL_Event> text_event_queue;
	inline std::vector<SDL_Event> event_queue;

	struct SliderObserver {
		bool init = false;
		float last_tied_value = 0.0f;
	};

	inline std::unordered_map<std::string, SliderObserver> slider_observers;

	inline const int TYPE_SWITCH_PADDING = 5;

	void render_bar(const Container& container, const AnimatedElement& element);

	void render_text(const Container& container, const AnimatedElement& element);

	void render_image(const Container& container, const AnimatedElement& element);

	void render_videos(const Container& container, const AnimatedElement& element);
	bool update_videos(const Container& container, AnimatedElement& element);
	void remove_videos(AnimatedElement& element);
	void pause_stale_videos(AnimatedElement& element);

	void handle_videos_event(const SDL_Event& event, bool& to_render);

	void render_button(const Container& container, const AnimatedElement& element);
	bool update_button(const Container& container, AnimatedElement& element);

	void render_icon_button(const Container& container, const AnimatedElement& element);
	bool update_icon_button(const Container& container, AnimatedElement& element);

	inline const int NOTIFICATION_DEFAULT_W = 270;

	void render_notification(const Container& container, const AnimatedElement& element);
	bool update_notification(const Container& container, AnimatedElement& element);

	void render_slider(const Container& container, const AnimatedElement& element);
	bool update_slider(const Container& container, AnimatedElement& element);
	void remove_slider(AnimatedElement& element);

	void render_seek_bar(const Container& container, const AnimatedElement& element);
	bool update_seek_bar(const Container& container, AnimatedElement& element);

	void render_text_input(const Container& container, const AnimatedElement& element);
	bool update_text_input(const Container& container, AnimatedElement& element);

	void render_checkbox(const Container& container, const AnimatedElement& element);
	bool update_checkbox(const Container& container, AnimatedElement& element);

	void render_dropdown(const Container& container, const AnimatedElement& element);
	bool update_dropdown(const Container& container, AnimatedElement& element);

	void render_color_picker(const Container& container, const AnimatedElement& element);
	bool update_color_picker(const Container& container, AnimatedElement& element);
	void remove_color_picker(AnimatedElement& element);

	// whether the color picker with this id is currently showing its popup
	bool is_color_picker_open(const Container& container, const std::string& id);

	void render_separator(const Container& container, const AnimatedElement& element);

	void render_weighting_graph(const Container& container, const AnimatedElement& element);

	void render_tabs(const Container& container, const AnimatedElement& element);
	bool update_tabs(const Container& container, AnimatedElement& element);

	void render_hint(const Container& container, const AnimatedElement& element);

	void render_spinner(const Container& container, const AnimatedElement& element);

	void render_update_notice(const Container& container, const AnimatedElement& element);

	void render_logo_and_version(const Container& container, const AnimatedElement& element);

	void render_link(const Container& container, const AnimatedElement& element);
	bool update_link(const Container& container, AnimatedElement& element);

	void reset_container(
		Container& container,
		SDL_Window* window,
		const gfx::Rect& rect,
		int element_gap,
		const std::optional<Padding>& padding = {},
		float line_height = 1.2f,
		std::optional<gfx::Color> background_color = {}
	);

	AnimatedElement* add_element(
		Container& container,
		Element&& _element,
		int margin_bottom,
		const std::unordered_map<size_t, AnimationState>& animations = { { hasher("main"), DEFAULT_ANIMATION } }
	);
	AnimatedElement* add_element(
		Container& container,
		Element&& _element,
		const std::unordered_map<size_t, AnimationState>& animations = { { hasher("main"), DEFAULT_ANIMATION } }
	);

	// elements
	AnimatedElement* add_bar(
		const std::string& id,
		Container& container,
		float percent_fill,
		gfx::Color background_color,
		gfx::Color fill_color,
		int bar_width,
		std::optional<std::string> bar_text = {},
		std::optional<gfx::Color> text_color = {},
		std::optional<render::Font> font = {}
	);

	AnimatedElement* add_text(
		const std::string& id,
		Container& container,
		const std::string& text,
		gfx::Color color,
		const render::Font& font,
		unsigned int flags = EFontFlags::FONT_NONE
	);

	AnimatedElement* add_text(
		const std::string& id,
		Container& container,
		std::vector<std::string> lines,
		gfx::Color color,
		const render::Font& font,
		unsigned int flags = EFontFlags::FONT_NONE
	);

	AnimatedElement* add_text_fixed(
		const std::string& id,
		Container& container,
		const gfx::Point& position,
		const std::string& text,
		gfx::Color color,
		const render::Font& font,
		unsigned int flags = EFontFlags::FONT_NONE
	);

	AnimatedElement* add_text_fixed(
		const std::string& id,
		Container& container,
		const gfx::Point& position,
		std::vector<std::string> lines,
		gfx::Color color,
		const render::Font& font,
		unsigned int flags = EFontFlags::FONT_NONE
	);

	std::optional<AnimatedElement*> add_image(
		const std::string& id,
		Container& container,
		const std::filesystem::path& image_path,
		const gfx::Size& max_size,
		std::string image_id = "",
		gfx::Color image_color = gfx::Color::white()
	); // use image_id to distinguish images that have the same filename and reload it (e.g. if its updated)

	std::optional<AnimatedElement*> add_image(
		const std::string& id,
		Container& container,
		std::shared_ptr<render::Texture> texture,
		const gfx::Size& max_size,
		const std::string& image_id = "",
		gfx::Color image_color = gfx::Color::white()
	);

	std::optional<AnimatedElement*> add_videos(
		const std::string& id,
		Container& container,
		const std::vector<UIVideo>& ui_videos,
		size_t& index,
		float& start,
		float& end,
		float& volume,
		bool trim_disabled,
		const std::function<void(size_t video_id)>& on_remove
	);

	AnimatedElement* add_button(
		const std::string& id,
		Container& container,
		const std::string& text,
		const render::Font& font,
		std::optional<std::function<void()>> on_press = {},
		std::optional<gfx::Color> accent_color = {}
	);

	AnimatedElement* add_icon_button(
		const std::string& id,
		Container& container,
		const std::string& icon,
		const render::Font& font,
		const gfx::Size& size,
		gfx::Color color,
		gfx::Color hover_color,
		std::optional<std::function<void()>> on_press = {},
		const std::string& tooltip = ""
	);

	AnimatedElement* add_notification(
		const std::string& id,
		Container& container,
		const std::string& text,
		NotificationType type,
		const render::Font& font,
		std::optional<std::function<void(const std::string& id)>> on_click = {},
		std::optional<std::function<void(const std::string& id)>> on_close = {}
	);

	AnimatedElement* add_slider(
		const std::string& id,
		Container& container,
		const std::variant<int, float>& min_value,
		const std::variant<int, float>& max_value,
		std::variant<int*, float*> value,
		const std::string& label_format,
		const render::Font& font,
		std::optional<std::function<void(const std::variant<int*, float*>&)>> on_change = {},
		float precision = 0.f,
		const std::string& tooltip = ""
	);

	AnimatedElement* add_slider_tied(
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
		std::optional<std::function<void(const std::variant<int*, float*>&)>> on_change = {},
		float precision = 0.f,
		const std::string& tooltip = ""
	);

	int seek_bar_height(const render::Font& font);

	AnimatedElement* add_seek_bar(
		const std::string& id,
		Container& container,
		float& value,
		const render::Font& font,
		float duration = 0.f,
		std::optional<int> width = {} // defaults to filling the container
	);

	// height of a text input's field using this font (excluding any label), for lining other elements up with one
	int text_input_height(const render::Font& font);

	AnimatedElement* add_text_input(
		const std::string& id,
		Container& container,
		std::string& text,
		const std::string& label,
		const render::Font& font,
		const std::string& placeholder = "",
		std::optional<std::function<void(const std::string&)>> on_change = {},
		bool read_only = false,
		std::optional<int> width = {} // defaults to filling the container
	);

	AnimatedElement* add_checkbox(
		const std::string& id,
		Container& container,
		const std::string& label,
		bool& checked,
		const render::Font& font,
		std::optional<std::function<void(bool)>> on_change = {}
	);

	AnimatedElement* add_dropdown(
		const std::string& id,
		Container& container,
		const std::string& label,
		const std::vector<std::string>& options,
		std::string& selected,
		const render::Font& font,
		std::optional<std::function<void(std::string*)>> on_change = {},
		const std::vector<std::string>& muted_options = {}
	);

	AnimatedElement* add_color_picker(
		const std::string& id,
		Container& container,
		const std::string& label,
		std::string& hex,
		const render::Font& font,
		gfx::Color default_color,
		const std::vector<gfx::Color>& presets = {},
		std::optional<std::function<void()>> on_change = {}
	);

	AnimatedElement* add_weighting_graph(
		const std::string& id, Container& container, const std::vector<double>& weights, bool accurate_fps
	);

	int tabs_height(const render::Font& font);

	AnimatedElement* add_tabs(
		const std::string& id,
		Container& container,
		const std::vector<std::string>& options,
		std::string& selected,
		const render::Font& font,
		std::optional<std::function<void()>> on_select = {}
	);

	AnimatedElement* add_hint(
		const std::string& id,
		Container& container,
		const std::vector<std::string>& paragraphs,
		gfx::Color color,
		const render::Font& font
	);

	AnimatedElement* add_separator(const std::string& id, Container& container, SeparatorStyle style);

	AnimatedElement* add_spinner(
		const std::string& id,
		Container& container,
		float radius = 8.f,
		gfx::Color background_color = gfx::Color::white(50),
		gfx::Color highlight_color = gfx::Color::white(),
		float thickness = 2.f,
		float trail_degrees = 180.f
	);

	AnimatedElement* add_update_notice(
		const std::string& id,
		Container& container,
		const std::string& status,
		const std::string& subtext,
		const std::vector<std::vector<UpdateNoticeLink>>& lines,
		std::optional<float> progress,
		const render::Font& font,
		UpdateNoticeAlign align = UpdateNoticeAlign::RIGHT
	);

	AnimatedElement* add_logo_and_version(
		const std::string& id,
		Container& container,
		std::shared_ptr<render::Texture> logo,
		const std::string& title,
		const std::string& subtitle,
		const render::Font& title_font,
		const render::Font& font
	);

	AnimatedElement* add_link(
		const std::string& id,
		Container& container,
		const std::string& text,
		const render::Font& font,
		std::optional<std::function<void()>> on_press = {},
		gfx::Color color = gfx::Color::white(105),
		gfx::Color hover_color = gfx::Color::white()
	);

	void add_spacing(Container& container, int spacing);

	void set_next_same_line(Container& container);

	void center_elements_in_container(Container& container, bool horizontal = true, bool vertical = true);

	void center_element(Container& container, AnimatedElement* animated_element);
	void center_elements(Container& container, const std::vector<AnimatedElement*>& animated_elements);

	void right_align_element(Container& container, AnimatedElement* animated_element);

	void anchor_elements_to_bottom(Container& container);

	std::vector<decltype(Container::elements)::iterator> get_sorted_container_elements(Container& container);

	void set_cursor(SDL_SystemCursor cursor);

	void set_active_element(AnimatedElement& element, const std::string& type = "");
	AnimatedElement* get_active_element();
	std::string get_active_element_type();
	bool is_active_element(const AnimatedElement& element, const std::string& type = "");
	void reset_active_element();

	bool set_hovered_element(AnimatedElement& element);
	std::string get_hovered_id();

	namespace tooltip {
		inline const float DELAY = 0.6f;

		void set(const std::string& text, const render::Font& font = fonts::dejavu);

		void on_input_end(const std::string& hovered_element_id);

		bool update(float delta_time);
		void render();
	}

	namespace dialog {
		struct Options {
			std::string title;
			std::string body;

			std::string detail;

			std::string confirm_text = "Confirm";
			std::string cancel_text = "Cancel";

			std::optional<gfx::Color> confirm_color;

			std::function<void()> on_confirm;
			std::function<void()> on_cancel;
		};

		void open(Options options);
		void close();
		bool is_open();

		void confirm_destructive(
			const std::string& title,
			const std::string& body,
			const std::string& confirm_text,
			std::function<void()> on_confirm,
			const std::string& detail = ""
		);

		void build(SDL_Window* window, const gfx::Rect& screen_rect);

		bool update_input();
		bool update_frame(float delta_time);
		void render();
	}

	bool update_container_input(Container& container);
	void on_update_input_start();
	void on_update_input_end();

	bool update_container_frame(Container& container, float delta_time);
	void on_update_frame_end();

	void render_container(Container& container);

	void on_frame_start();
	void on_frame_end();

	void reset_tied_sliders();
}
