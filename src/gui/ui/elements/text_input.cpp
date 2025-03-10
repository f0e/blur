#include "../ui.h"
#include "../render.h"
#include "../utils.h"
#include "../keys.h"

#include "../../renderer.h"

// NOLINTBEGIN ai todo: refactor it all but just want functionality rn

// Advanced text editing capabilities
class TextEditor {
public:
	enum class SelectionMode {
		None,
		Character,
		Word,
		Line
	};

private:
	std::string* text_buffer;
	size_t cursor_pos = 0;
	size_t selection_start = 0;
	size_t selection_end = 0;
	SelectionMode selection_mode = SelectionMode::None;

public:
	TextEditor(std::string* buffer) : text_buffer(buffer) {}

	// Add getter for cursor position
	[[nodiscard]] size_t get_cursor_position() const {
		return cursor_pos;
	}

	// Modify key event handling to use the new key event structure
	bool handle_key_event(const keys::KeyPress& key_event) {
		// // Handle key repeat
		// if (key_event.repeat) {
		// 	// Optional: implement repeat behavior or ignore
		// 	return false;
		// }

		bool pressing_ctrl = (key_event.modifiers & os::KeyModifiers::kKeyAltModifier) != 0;

		// Use scancode for special keys
		switch (key_event.scancode) {
			case os::kKeyLeft:
				move_cursor_left(pressing_ctrl);
				return true;

			case os::kKeyRight:
				move_cursor_right(pressing_ctrl);
				return true;

			case os::kKeyUp:
				move_cursor_up();
				return true;

			case os::kKeyDown:
				move_cursor_down();
				return true;

			case os::kKeyHome:
				move_to_line_start(pressing_ctrl);
				return true;

			case os::kKeyEnd:
				move_to_line_end(pressing_ctrl);
				return true;

			// Deletion
			case os::kKeyBackspace:
				handle_backspace(pressing_ctrl);
				return true;

			case os::kKeyDel:
				handle_delete(pressing_ctrl);
				return true;

			case 'A': // Ctrl+A (Select All)
				if (pressing_ctrl) {
					select_all();
					return true;
				}
				break;

			default:
				break;
		}

		// Handle unicode character input
		if (key_event.unicode_char >= 32 && key_event.unicode_char < 127) {
			return handle_text_input(key_event.unicode_char);
		}

		return false;
	}

	bool handle_text_input(char unicode) {
		// Filter out control characters
		if (unicode < 32 || unicode > 126)
			return false;

		// Remove any selected text first
		if (has_selection()) {
			delete_selection();
		}

		// Insert character at cursor
		text_buffer->insert(text_buffer->begin() + cursor_pos, unicode);
		cursor_pos++;

		return true;
	}

	// Cursor Movement
	void move_cursor_left(bool by_word = false) {
		if (by_word) {
			cursor_pos = find_previous_word_boundary();
		}
		else if (cursor_pos > 0) {
			cursor_pos--;
		}
	}

	void move_cursor_right(bool by_word = false) {
		if (by_word) {
			cursor_pos = find_next_word_boundary();
		}
		else if (cursor_pos < text_buffer->length()) {
			cursor_pos++;
		}
	}

	void move_cursor_up() {
		// For simplicity, moves to start of text
		// In a full implementation, this would navigate lines
		cursor_pos = 0;
	}

	void move_cursor_down() {
		// Moves to end of text
		cursor_pos = text_buffer->length();
	}

	void move_to_line_start(bool document_start = false) {
		if (document_start) {
			cursor_pos = 0;
		}
		else {
			// Find start of current line
			while (cursor_pos > 0 && (*text_buffer)[cursor_pos - 1] != '\n') {
				cursor_pos--;
			}
		}
	}

	void move_to_line_end(bool document_end = false) {
		if (document_end) {
			cursor_pos = text_buffer->length();
		}
		else {
			// Find end of current line
			while (cursor_pos < text_buffer->length() && (*text_buffer)[cursor_pos] != '\n') {
				cursor_pos++;
			}
		}
	}

	// Deletion Methods
	void handle_backspace(bool by_word = false) {
		if (has_selection()) {
			delete_selection();
			return;
		}

		if (cursor_pos > 0) {
			size_t delete_start = by_word ? find_previous_word_boundary() : cursor_pos - 1;
			text_buffer->erase(text_buffer->begin() + delete_start, text_buffer->begin() + cursor_pos);
			cursor_pos = delete_start;
		}
	}

	void handle_delete(bool by_word = false) {
		if (has_selection()) {
			delete_selection();
			return;
		}

		if (cursor_pos < text_buffer->length()) {
			size_t delete_end = by_word ? find_next_word_boundary() : cursor_pos + 1;
			text_buffer->erase(text_buffer->begin() + cursor_pos, text_buffer->begin() + delete_end);
		}
	}

	// Selection Methods
	bool has_selection() const {
		return selection_mode != SelectionMode::None && selection_start != selection_end;
	}

	void clear_selection() {
		selection_mode = SelectionMode::None;
		selection_start = selection_end = cursor_pos;
	}

	void select_all() {
		selection_start = 0;
		selection_end = text_buffer->length();
		cursor_pos = selection_end;
		selection_mode = SelectionMode::Character;
	}

	void delete_selection() {
		if (!has_selection())
			return;

		size_t start = std::min(selection_start, selection_end);
		size_t end = std::max(selection_start, selection_end);

		text_buffer->erase(text_buffer->begin() + start, text_buffer->begin() + end);

		cursor_pos = start;
		clear_selection();
	}

private:
	// big   chungus
	static bool is_word_char(int ch) {
		return (ch && !std::isspace(ch) && !std::ispunct(ch));
	}

	size_t find_next_word_boundary() {
		size_t pos = cursor_pos;
		size_t textlen = text_buffer->length();

		// Skip non-word characters
		while (pos < textlen && !is_word_char((*text_buffer)[pos])) {
			++pos;
		}

		// Skip word characters
		while (pos < textlen && is_word_char((*text_buffer)[pos])) {
			++pos;
		}

		return pos;
	}

	size_t find_previous_word_boundary() {
		size_t pos = cursor_pos;

		// Skip non-word characters
		while (pos > 0 && !is_word_char((*text_buffer)[pos - 1])) {
			--pos;
		}

		// Skip word characters
		while (pos > 0 && is_word_char((*text_buffer)[pos - 1])) {
			--pos;
		}

		return pos;
	}
};

// Global text input management
class TextInputManager {
private:
	std::unordered_map<const ui::AnimatedElement*, TextEditor> editors;

public:
	void register_text_input(const ui::AnimatedElement* element, std::string* text_buffer) {
		editors.emplace(element, text_buffer);
	}

	std::optional<TextEditor*> get_editor(const ui::AnimatedElement* element) {
		auto it = editors.find(element);
		if (it != editors.end()) {
			return &it->second;
		}
		return {};
	}

	size_t get_cursor_position(const ui::AnimatedElement* element) {
		if (auto editor = get_editor(element))
			return (*editor)->get_cursor_position();

		return 0;
	}

	bool handle_key_event(const ui::AnimatedElement* element, keys::KeyPress key_event) {
		if (auto editor = get_editor(element))
			return (*editor)->handle_key_event(key_event);

		return false;
	}
};

// Singleton text input manager
static TextInputManager text_input_manager;

// NOLINTEND

void ui::render_text_input(const Container& container, os::Surface* surface, const AnimatedElement& element) {
	const float input_rounding = 5.0f;
	const float cursor_width = 2.0f;

	const auto& input_data = std::get<TextInputElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;
	float focus_anim = element.animations.at(hasher("focus")).current;

	gfx::Color cursor_color = utils::adjust_color(gfx::rgba(255, 255, 255, 255), anim);

	// Background color
	int base_shade = 30 + (10 * hover_anim);
	gfx::Color bg_color = utils::adjust_color(gfx::rgba(base_shade, base_shade, base_shade, 255), anim);
	gfx::Color border_color = utils::adjust_color(
		gfx::rgba(100 + (50 * focus_anim), 100 + (50 * focus_anim), 100 + (50 * focus_anim), 255), anim
	);

	// Render input background and border
	render::rounded_rect_filled(surface, element.element->rect, bg_color, input_rounding);
	render::rounded_rect_stroke(surface, element.element->rect, border_color, input_rounding);

	// Determine text to render (text or placeholder)
	std::string display_text = (*input_data.text).empty() ? input_data.placeholder : (*input_data.text);
	gfx::Color text_color = utils::adjust_color(
		(*input_data.text).empty() ? gfx::rgba(150, 150, 150, 255) : gfx::rgba(255, 255, 255, 255), anim
	);

	// Calculate text position
	gfx::Point text_pos = element.element->rect.center();
	text_pos.y += input_data.font.getSize() / 2 - 1;
	text_pos.x = element.element->rect.x + 10; // Left padding

	// Render text
	render::text(surface, text_pos, text_color, display_text, input_data.font, os::TextAlign::Left);

	// Update cursor rendering to reflect new cursor position
	if (active_element == &element) {
		// Get text up to cursor position
		std::string text_before_cursor = (*input_data.text).substr(0, text_input_manager.get_cursor_position(&element));
		gfx::Size text_size = render::get_text_size(text_before_cursor, input_data.font);

		gfx::Rect cursor_rect(
			text_pos.x + text_size.w, element.element->rect.y + 5, cursor_width, element.element->rect.h - 10
		);
		render::rect_filled(surface, cursor_rect, cursor_color);
	}
}

bool ui::update_text_input(const Container& container, AnimatedElement& element) {
	auto& input_data = std::get<TextInputElementData>(element.element->data);
	auto& hover_anim = element.animations.at(hasher("hover"));
	auto& focus_anim = element.animations.at(hasher("focus"));

	bool hovered = element.element->rect.contains(keys::mouse_pos);
	hover_anim.set_goal(hovered ? 1.f : 0.f);

	if (hovered)
		set_cursor(os::NativeCursor::IBeam);

	// Handle mouse click to focus/unfocus
	if (hovered && keys::is_mouse_down()) {
		active_element = &element;
		focus_anim.set_goal(1.f);
	}
	else if (keys::is_mouse_down() && !hovered) {
		active_element = nullptr;
		focus_anim.set_goal(0.f);
	}

	bool active = active_element == &element;

	if (active) {
		// Register text input if not already done
		static std::unordered_map<AnimatedElement*, bool> registered_inputs;
		if (!registered_inputs[&element]) {
			text_input_manager.register_text_input(&element, input_data.text);
			registered_inputs[&element] = true;
		}

		// todo: click to move cursor

		// Process pressed keys
		for (const auto& key : keys::pressed_keys) {
			// Try key event handling
			if (text_input_manager.handle_key_event(&element, key)) {
				// Trigger on_change callback
				if (input_data.on_change) {
					(*input_data.on_change)(*input_data.text);
				}
			}
		}

		keys::pressed_keys.clear();
	}

	return active;
}

ui::Element& ui::add_text_input(
	const std::string& id,
	Container& container,
	std::string& text,
	const std::string& placeholder,
	const SkFont& font,
	std::optional<std::function<void(const std::string&)>> on_change
) {
	const gfx::Size input_size(200, font.getSize() + 12);

	Element element = {
		.type = ElementType::TEXT_INPUT,
		.rect = gfx::Rect(container.current_position, input_size),
		.data =
			TextInputElementData{
				.text = &text,
				.placeholder = placeholder,
				.font = font,
				.on_change = std::move(on_change),
			},
		.render_fn = render_text_input,
		.update_fn = update_text_input,
	};

	return *add_element(
		container,
		id,
		std::move(element),
		container.line_height,
		{
			{ hasher("main"), { .speed = 25.f } },
			{ hasher("hover"), { .speed = 80.f } },
			{ hasher("focus"), { .speed = 25.f } },
		}
	);
}
