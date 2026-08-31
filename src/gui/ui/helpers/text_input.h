#pragma once

#include "gui/render/font/font.h"

namespace ui::helpers::text_input {
	struct TextInputData;
}

// we use dear imgui's fork of stb_textedit rather than upstream, because it adds UTF-8 support (cursor movement
// and deletion step over whole codepoints instead of bytes) plus fixes for redo discarding and last-line charpos.
// the fork reads two members straight off the string object, so those have to keep imgui's names.
struct STB_TexteditState;

#define IMSTB_TEXTEDIT_STRING         ui::helpers::text_input::TextInputData
#define IMSTB_TEXTEDIT_CHARTYPE       char
#define IMSTB_TEXTEDIT_POSITIONTYPE   int
#define IMSTB_TEXTEDIT_UNDOSTATECOUNT 99
#define IMSTB_TEXTEDIT_UNDOCHARCOUNT  999

// only the type configuration lives here, since it's needed to declare STB_TexteditState below. the callback
// and key macros stay in text_input.cpp so they don't leak into every TU that pulls in ui.h.
#include <imstb_textedit.h>

namespace ui::helpers::text_input {
	struct TextInputData {
		std::string* text{};
		render::Font font{};
		std::optional<std::function<void(const std::string&)>> on_change;
		bool read_only = false; // still focusable & selectable, just can't be edited
		bool multiline = false;

		// these two are named for imgui's stb_textedit fork, which reads them directly out of the string object
		// (see stb_textedit_click/_drag/_find_charpos). Stb points back at the owning state's edit_state.
		STB_TexteditState* Stb{};
		ImS8 LastMoveDirectionLR = ImGuiDir_None;

		bool operator==(const TextInputData& other) const {
			return text == other.text && font == other.font && read_only == other.read_only &&
			       multiline == other.multiline;
		}
	};

	struct TextInputStateInternal {
		STB_TexteditState edit_state{};
		bool active = false;
		std::string composition;   // For IME
		int ime_cursor = 0;        // Cursor within IME composition
		int ime_selection_len = 0; // Selection length within IME composition

		// storage for fields whose text is generated rather than owned by the caller (see add_selectable_text). it
		// lives with the edit state so it stays valid for as long as the element does, including while it fades out
		std::string owned_text;

		float scroll_x = 0.f;                 // horizontal scroll in pixels, kept sticky across frames
		bool cursor_follow = false;           // scroll to reveal the cursor on the next render
		float cursor_anim = 0.f;              // blink timer, negative means "solid on" right after an action
		bool selected_all_mouse_lock = false; // after select-all, ignore drags until the button comes back up
		gfx::Point last_cursor_screen_pos;    // where the caret last drew, used to place the IME candidate window
	};

	inline std::unordered_map<std::string, TextInputStateInternal> text_input_map;

	float get_cursor_x(
		const ui::helpers::text_input::TextInputData& input_data, int cursor_pos, const gfx::Point& text_start_pos
	);

	void handle_text_input_event(TextInputData& input_data, TextInputStateInternal& state, const SDL_Event& event);

	// click / double-click (select word) / triple-click (select line) / shift-click (extend) / drag.
	// text_relative_pos is the mouse position relative to the text origin, scroll already applied.
	// `pressed` is passed in rather than read from keys:: because the caller claims the press (which clears it)
	// before we get here.
	void handle_mouse(
		TextInputData& input_data,
		TextInputStateInternal& state,
		const gfx::Point& text_relative_pos,
		bool hovered,
		bool pressed,
		int click_count,
		bool shift
	);

	bool has_selection(const STB_TexteditState& state);

	void click(TextInputData* str, STB_TexteditState* state, float x, float y);
	void drag(TextInputData* str, STB_TexteditState* state, float x, float y);
	void clamp(TextInputData* str, STB_TexteditState* state);
	void select_all(TextInputData* str, STB_TexteditState* state);

	// call after any user action so the caret shows solid before resuming its blink
	void cursor_anim_reset(TextInputStateInternal& state);

	TextInputStateInternal& add_text_edit(const std::string& id, TextInputData& input_data);
	bool has_text_edit(const std::string& id);
	void remove_text_edit(const std::string& id);

	bool has_active_text_edit(const std::string& id);

	// pins the OS IME candidate window to the caret instead of the whole field
	void update_ime_area(SDL_Window* window, const TextInputStateInternal& state, const render::Font& font);

	void render_text(
		const TextInputData& input_data,
		TextInputStateInternal& state,
		gfx::Point text_pos,
		const gfx::Color& text_color,
		const gfx::Rect& clip_rect,
		const std::string& placeholder,
		const gfx::Color& placeholder_color,
		const gfx::Color& selection_colour,
		const gfx::Color& composition_text_color,
		const gfx::Color& composition_bg_color
	);
}
