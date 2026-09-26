#include "text_input.h"
#include "../keys.h"
#include "../../render/render.h"

// scroll in chunks so the text doesn't jitter at the edge
constexpr float SCROLL_CHUNK_FRACTION = 0.25f;

constexpr float CURSOR_BLINK_PERIOD = 1.2f; // seconds for a full blink cycle
constexpr float CURSOR_BLINK_ON = 0.8f;     // how much of that cycle the caret is visible for
constexpr float CURSOR_BLINK_SOLID = -0.3f; // grace period after an action where the caret stays solid
constexpr float CURSOR_WIDTH = 1.f;

namespace {
	// utf-8
	// not using imgui's ImTextCharFromUtf8 since imgui_internal.h conflicts with our stb config

	int utf8_seq_len(unsigned char lead) {
		if (lead < 0x80)
			return 1;
		if ((lead & 0xE0) == 0xC0)
			return 2;
		if ((lead & 0xF0) == 0xE0)
			return 3;
		if ((lead & 0xF8) == 0xF0)
			return 4;
		return 1; // invalid lead byte
	}

	bool utf8_is_continuation(unsigned char c) {
		return (c & 0xC0) == 0x80;
	}

	unsigned int utf8_decode(const char* p, const char* end) {
		if (p >= end)
			return 0;

		auto lead = static_cast<unsigned char>(*p);
		int len = utf8_seq_len(lead);
		if (len == 1 || p + len > end)
			return lead;

		static constexpr std::array<unsigned int, 5> LEAD_MASK = { 0, 0x7F, 0x1F, 0x0F, 0x07 };
		unsigned int c = lead & LEAD_MASK.at(len);
		for (int i = 1; i < len; ++i)
			c = (c << 6) | (static_cast<unsigned char>(p[i]) & 0x3F);

		return c;
	}

	int string_len(IMSTB_TEXTEDIT_STRING* str) {
		return str && str->text ? static_cast<int>(str->text->length()) : 0;
	}

	int textedit_getnextcharindex(IMSTB_TEXTEDIT_STRING* str, int idx) {
		int len = string_len(str);
		if (idx >= len)
			return len + 1; // matches imgui

		int next = idx + utf8_seq_len(static_cast<unsigned char>((*str->text)[idx]));
		return std::min(next, len);
	}

	int textedit_getprevcharindex(IMSTB_TEXTEDIT_STRING* str, int idx) {
		if (idx <= 0)
			return -1;

		int i = idx - 1;
		// walk back over continuation bytes, capped since a codepoint is at most 4 bytes
		for (int steps = 0; i > 0 && steps < 3 && utf8_is_continuation(static_cast<unsigned char>((*str->text)[i]));
		     ++steps)
			i--;

		return i;
	}
}

#define IMSTB_TEXTEDIT_GETNEXTCHARINDEX textedit_getnextcharindex
#define IMSTB_TEXTEDIT_GETPREVCHARINDEX textedit_getprevcharindex
#define IMSTB_TEXTEDIT_GETWIDTH_NEWLINE (-1.0f)

namespace {
	// measurement
	// float widths since stb compares summed character widths against whole run widths

	float measure(const render::Font& font, const char* begin, const char* end) {
		if (!font || begin >= end)
			return 0.f;

		return font.im_font()->CalcTextSizeA(font.size(), FLT_MAX, 0.f, begin, end).x;
	}

	float measure(const render::Font& font, const std::string& text, int offset, int count) {
		const char* data = text.data();
		return measure(font, data + offset, data + offset + count);
	}

	struct TextPosition {
		float x;
		int line;
	};

	TextPosition get_text_position(const ui::helpers::text_input::TextInputData& input_data, int cursor_pos) {
		if (!input_data.text || !input_data.font)
			return {};

		const std::string& text = *input_data.text;
		cursor_pos = std::clamp(cursor_pos, 0, static_cast<int>(text.length()));

		int line_start = 0;
		int line = 0;
		for (int i = 0; i < cursor_pos; i++) {
			if (text[i] == '\n') {
				line_start = i + 1;
				line++;
			}
		}

		return {
			.x = measure(input_data.font, text, line_start, cursor_pos - line_start),
			.line = line,
		};
	}

	// stb_textedit callbacks

	void textedit_layoutrow(StbTexteditRow* r, IMSTB_TEXTEDIT_STRING* str, int n) {
		if (!str || !str->font || !str->text) {
			r->num_chars = 0;
			r->x0 = r->x1 = 0;
			r->baseline_y_delta = 1.f;
			r->ymin = 0;
			r->ymax = 1.f;
			return;
		}

		const render::Font& font = str->font;
		const std::string& text = *str->text;
		int len = static_cast<int>(text.length());

		int start_char = std::clamp(n, 0, len);

		// single line: run to the next newline, or the end of the string
		int end_char = len;
		for (int i = start_char; i < len; ++i) {
			if (text[i] == '\n') {
				end_char = i + 1; // the newline belongs to this row
				break;
			}
		}

		r->num_chars = std::max(end_char - start_char, 0);

		// measure without the trailing newline
		int measure_count = r->num_chars;
		if (measure_count > 0 && text[start_char + measure_count - 1] == '\n')
			measure_count--;

		r->x0 = 0.f; // relative to the text origin
		r->x1 = measure(font, text, start_char, measure_count);
		r->baseline_y_delta = static_cast<float>(font.height());
		r->ymin = 0.f;
		r->ymax = static_cast<float>(font.height());
	}

	float textedit_getwidth(IMSTB_TEXTEDIT_STRING* str, int n, int i) {
		if (!str || !str->font || !str->text)
			return 0.f;

		int len = string_len(str);
		int idx = n + i;
		if (idx < 0 || idx >= len)
			return 0.f;

		if ((*str->text)[idx] == '\n')
			return IMSTB_TEXTEDIT_GETWIDTH_NEWLINE;

		// measure the whole codepoint
		int seq = std::min(utf8_seq_len(static_cast<unsigned char>((*str->text)[idx])), len - idx);
		return measure(str->font, *str->text, idx, seq);
	}

	void textedit_deletechars(IMSTB_TEXTEDIT_STRING* str, int i, int n) {
		if (!str || !str->text || str->read_only)
			return;

		int len = string_len(str);
		i = std::max(i, 0);
		if (i >= len)
			return;

		n = std::min(n, len - i);
		if (n <= 0)
			return;

		str->text->erase(i, n);

		if (str->on_change)
			(*str->on_change)(*str->text);
	}

	// returns the number of characters inserted
	int textedit_insertchars(IMSTB_TEXTEDIT_STRING* str, int i, const IMSTB_TEXTEDIT_CHARTYPE* c, int n) {
		if (!str || !str->text || str->read_only || n <= 0)
			return 0;

		i = std::clamp(i, 0, string_len(str));
		str->text->insert(i, std::string(c, n));

		if (str->on_change)
			(*str->on_change)(*str->text);

		return n;
	}

	// word movement
	// ported from imgui's InputTextEx

	bool is_blank(unsigned int c) {
		return c == ' ' || c == '\t' || c == 0x3000; // includes the ideographic space
	}

	bool is_separator(unsigned int c) {
		static constexpr std::array<unsigned int, 29> SEPARATORS = {
			',', 0x3001, '.', 0x3002, ';', 0xFF1B, '(',  0xFF08, ')', 0xFF09, '{',    0xFF5B, '}',  0xFF5D, '[', 0x300C,
			']', 0x300D, '|', 0xFF5C, '!', 0xFF01, '\\', 0xFFE5, '/', 0x30FB, 0xFF0F, '\n',   '\r',
		};

		return std::ranges::find(SEPARATORS, c) != SEPARATORS.end();
	}

	unsigned int char_at(IMSTB_TEXTEDIT_STRING* str, int idx) {
		const char* data = str->text->data();
		return utf8_decode(data + idx, data + string_len(str));
	}

	bool is_word_boundary_from_right(IMSTB_TEXTEDIT_STRING* str, int idx) {
		if (idx <= 0)
			return false;

		unsigned int curr_c = char_at(str, idx);
		unsigned int prev_c = char_at(str, std::max(textedit_getprevcharindex(str, idx), 0));

		bool prev_white = is_blank(prev_c);
		bool prev_separ = is_separator(prev_c);
		bool curr_white = is_blank(curr_c);
		bool curr_separ = is_separator(curr_c);

		return ((prev_white || prev_separ) && !(curr_separ || curr_white)) || (curr_separ && !prev_separ);
	}

	bool is_word_boundary_from_left(IMSTB_TEXTEDIT_STRING* str, int idx) {
		if (idx <= 0)
			return false;

		unsigned int prev_c = char_at(str, idx);
		unsigned int curr_c = char_at(str, std::max(textedit_getprevcharindex(str, idx), 0));

		bool prev_white = is_blank(prev_c);
		bool prev_separ = is_separator(prev_c);
		bool curr_white = is_blank(curr_c);
		bool curr_separ = is_separator(curr_c);

		return (prev_white && !(curr_separ || curr_white)) || (curr_separ && !prev_separ);
	}

	int move_word_left(IMSTB_TEXTEDIT_STRING* str, int idx) {
		idx = textedit_getprevcharindex(str, idx);
		while (idx >= 0 && !is_word_boundary_from_right(str, idx))
			idx = textedit_getprevcharindex(str, idx);

		return idx < 0 ? 0 : idx;
	}

	// mac stops at the end of a word, windows at the start of the next one
	int move_word_right_mac(IMSTB_TEXTEDIT_STRING* str, int idx) {
		int len = string_len(str);
		idx = textedit_getnextcharindex(str, idx);
		while (idx < len && !is_word_boundary_from_left(str, idx))
			idx = textedit_getnextcharindex(str, idx);

		return std::min(idx, len);
	}

	int move_word_right_win(IMSTB_TEXTEDIT_STRING* str, int idx) {
		int len = string_len(str);
		idx = textedit_getnextcharindex(str, idx);
		while (idx < len && !is_word_boundary_from_right(str, idx))
			idx = textedit_getnextcharindex(str, idx);

		return std::min(idx, len);
	}

	int move_word_right(IMSTB_TEXTEDIT_STRING* str, int idx) {
#ifdef __APPLE__
		return move_word_right_mac(str, idx);
#else
		return move_word_right_win(str, idx);
#endif
	}
}

// NOLINTBEGIN(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum) stb is configured through macros
#define STB_TEXTEDIT_STRINGLEN(obj)  string_len(obj)
#define STB_TEXTEDIT_LAYOUTROW       textedit_layoutrow
#define STB_TEXTEDIT_GETWIDTH        textedit_getwidth
#define STB_TEXTEDIT_GETCHAR(obj, i) ((*(obj)->text)[(i)])
#define STB_TEXTEDIT_NEWLINE         '\n'
#define STB_TEXTEDIT_DELETECHARS     textedit_deletechars
#define STB_TEXTEDIT_INSERTCHARS     textedit_insertchars
#define STB_TEXTEDIT_IS_SPACE(ch)    is_blank(static_cast<unsigned char>(ch))
#define STB_TEXTEDIT_MOVEWORDLEFT    move_word_left
#define STB_TEXTEDIT_MOVEWORDRIGHT   move_word_right

// STB_TEXTEDIT_KEYTOTEXT is left undefined, character input goes through stb_textedit_text() instead

// sentinel values that don't overlap sdl scancodes
#define STB_TEXTEDIT_K_LEFT      0x200000
#define STB_TEXTEDIT_K_RIGHT     0x200001
#define STB_TEXTEDIT_K_UP        0x200002
#define STB_TEXTEDIT_K_DOWN      0x200003
#define STB_TEXTEDIT_K_LINESTART 0x200004
#define STB_TEXTEDIT_K_LINEEND   0x200005
#define STB_TEXTEDIT_K_TEXTSTART 0x200006
#define STB_TEXTEDIT_K_TEXTEND   0x200007
#define STB_TEXTEDIT_K_DELETE    0x200008
#define STB_TEXTEDIT_K_BACKSPACE 0x200009
#define STB_TEXTEDIT_K_UNDO      0x20000A
#define STB_TEXTEDIT_K_REDO      0x20000B
#define STB_TEXTEDIT_K_WORDLEFT  0x20000C
#define STB_TEXTEDIT_K_WORDRIGHT 0x20000D
#define STB_TEXTEDIT_K_PGUP      0x20000E
#define STB_TEXTEDIT_K_PGDOWN    0x20000F
#define STB_TEXTEDIT_K_SHIFT     0x400000
// NOLINTEND(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum)

#define IMSTB_TEXTEDIT_IMPLEMENTATION
#include <imstb_textedit.h>

namespace {
	// returns 0 for keys that aren't text editing keys. shortcuts are handled in handle_text_input_event
	int translate_key(SDL_Scancode scan, bool ctrl, bool shift, bool alt, bool super) {
#ifdef __APPLE__
		// mac: alt moves by word, cmd jumps to line/document bounds
		bool word_move = alt;
		bool doc_move = super;
#else
		bool word_move = ctrl;
		bool doc_move = ctrl;
		(void)alt;
		(void)super;
#endif

		int key = 0;

		switch (scan) {
			case SDL_SCANCODE_LEFT:
				if (word_move)
					key = STB_TEXTEDIT_K_WORDLEFT;
				else if (doc_move)
					key = STB_TEXTEDIT_K_LINESTART;
				else
					key = STB_TEXTEDIT_K_LEFT;
				break;
			case SDL_SCANCODE_RIGHT:
				if (word_move)
					key = STB_TEXTEDIT_K_WORDRIGHT;
				else if (doc_move)
					key = STB_TEXTEDIT_K_LINEEND;
				else
					key = STB_TEXTEDIT_K_RIGHT;
				break;
			case SDL_SCANCODE_UP:
				key = doc_move ? STB_TEXTEDIT_K_TEXTSTART : STB_TEXTEDIT_K_UP;
				break;
			case SDL_SCANCODE_DOWN:
				key = doc_move ? STB_TEXTEDIT_K_TEXTEND : STB_TEXTEDIT_K_DOWN;
				break;
			case SDL_SCANCODE_HOME:
				key = ctrl ? STB_TEXTEDIT_K_TEXTSTART : STB_TEXTEDIT_K_LINESTART;
				break;
			case SDL_SCANCODE_END:
				key = ctrl ? STB_TEXTEDIT_K_TEXTEND : STB_TEXTEDIT_K_LINEEND;
				break;
			case SDL_SCANCODE_PAGEUP:
				key = STB_TEXTEDIT_K_PGUP;
				break;
			case SDL_SCANCODE_PAGEDOWN:
				key = STB_TEXTEDIT_K_PGDOWN;
				break;
			case SDL_SCANCODE_DELETE:
				key = STB_TEXTEDIT_K_DELETE;
				break;
			case SDL_SCANCODE_BACKSPACE:
				key = STB_TEXTEDIT_K_BACKSPACE;
				break;
			default:
				return 0;
		}

		if (shift)
			key |= STB_TEXTEDIT_K_SHIFT;

		return key;
	}

	bool key_edits_text(int key) {
		int base = key & ~STB_TEXTEDIT_K_SHIFT;
		return base == STB_TEXTEDIT_K_DELETE || base == STB_TEXTEDIT_K_BACKSPACE || base == STB_TEXTEDIT_K_UNDO ||
		       base == STB_TEXTEDIT_K_REDO;
	}

	void copy_selection_to_clipboard(
		const ui::helpers::text_input::TextInputData& input_data, const STB_TexteditState& edit_state
	) {
		int start = edit_state.select_start;
		int end = edit_state.select_end;
		if (start > end)
			std::swap(start, end);

		SDL_SetClipboardText(input_data.text->substr(start, end - start).c_str());
	}
}

float ui::helpers::text_input::get_cursor_x(
	const ui::helpers::text_input::TextInputData& input_data, int cursor_pos, const gfx::Point& text_start_pos
) {
	return static_cast<float>(text_start_pos.x) + get_text_position(input_data, cursor_pos).x;
}

bool ui::helpers::text_input::has_selection(const STB_TexteditState& state) {
	return STB_TEXT_HAS_SELECTION(&state);
}

void ui::helpers::text_input::click(IMSTB_TEXTEDIT_STRING* str, STB_TexteditState* state, float x, float y) {
	stb_textedit_click(str, state, x, y);
}

void ui::helpers::text_input::drag(IMSTB_TEXTEDIT_STRING* str, STB_TexteditState* state, float x, float y) {
	stb_textedit_drag(str, state, x, y);
}

void ui::helpers::text_input::clamp(IMSTB_TEXTEDIT_STRING* str, STB_TexteditState* state) {
	stb_textedit_clamp(str, state);
}

void ui::helpers::text_input::select_all(IMSTB_TEXTEDIT_STRING* str, STB_TexteditState* state) {
	state->select_start = 0;
	state->select_end = string_len(str);
	state->cursor = state->select_end;
	state->has_preferred_x = 0;
}

void ui::helpers::text_input::cursor_anim_reset(TextInputStateInternal& state) {
	// negative means the caret draws solid for a moment before it starts blinking again
	state.cursor_anim = CURSOR_BLINK_SOLID;
}

void ui::helpers::text_input::handle_mouse(
	TextInputData& input_data,
	TextInputStateInternal& state,
	const gfx::Point& text_relative_pos,
	bool hovered,
	bool pressed,
	int click_count,
	bool shift
) {
	// element data can be rebuilt between frames, so keep this pointing at the current state
	input_data.Stb = &state.edit_state;

	auto x = static_cast<float>(text_relative_pos.x);
	auto y = static_cast<float>(text_relative_pos.y);

	int local_click_count = 0;
	if (pressed && hovered && !shift) {
		// hit test first so a double click has to land on the same character. the press id makes sure the previous
		// press was also ours. copy the state since slider/color inputs select all on this press
		auto hit_test_state = state.edit_state;
		input_data.Stb = &hit_test_state;
		stb_textedit_click(&input_data, &hit_test_state, x, y);
		input_data.Stb = &state.edit_state;
		int clicked_cursor = hit_test_state.cursor;

		auto press_id = keys::get_mouse_press_id();
		bool continues_local_sequence = click_count >= 2 && state.last_mouse_press_id != 0 &&
		                                state.last_mouse_press_id + 1 == press_id &&
		                                state.last_mouse_click_cursor == clicked_cursor;
		local_click_count = continues_local_sequence ? state.local_mouse_click_count + 1 : 1;

		state.last_mouse_press_id = press_id;
		state.last_mouse_click_cursor = clicked_cursor;
		state.local_mouse_click_count = local_click_count;
	}
	else if (pressed && hovered) {
		// modified clicks extend the selection instead
		state.last_mouse_press_id = keys::get_mouse_press_id();
		state.last_mouse_click_cursor = -1;
		state.local_mouse_click_count = 0;
	}

	// only on a fresh press, using the local click count
	if (local_click_count >= 2) {
		stb_textedit_click(&input_data, &state.edit_state, x, y);

		// alternate word / line selection as the click count keeps going up, matching imgui
		if ((local_click_count - 2) % 2 == 0) {
			// double click: select the word. always uses the mac style word advance
			bool at_line_start =
				state.edit_state.cursor == 0 || (*input_data.text)[state.edit_state.cursor - 1] == '\n';

			if (has_selection(state.edit_state) || !at_line_start)
				stb_textedit_key(&input_data, &state.edit_state, STB_TEXTEDIT_K_WORDLEFT);

			if (!has_selection(state.edit_state))
				stb_textedit_prep_selection_at_cursor(&state.edit_state);

			state.edit_state.cursor = move_word_right_mac(&input_data, state.edit_state.cursor);
			state.edit_state.select_end = state.edit_state.cursor;
			stb_textedit_clamp(&input_data, &state.edit_state);
		}
		else {
			// triple click: select the whole line
			stb_textedit_key(&input_data, &state.edit_state, STB_TEXTEDIT_K_LINESTART);
			stb_textedit_key(&input_data, &state.edit_state, STB_TEXTEDIT_K_LINEEND | STB_TEXTEDIT_K_SHIFT);
		}

		state.selected_all_mouse_lock = true;
		cursor_anim_reset(state);
		return;
	}

	if (pressed) {
		if (hovered && !state.selected_all_mouse_lock) {
			// shift-click extends the existing selection instead of starting a new one
			if (shift)
				stb_textedit_drag(&input_data, &state.edit_state, x, y);
			else
				stb_textedit_click(&input_data, &state.edit_state, x, y);

			cursor_anim_reset(state);
		}
	}
	else if (keys::is_mouse_dragging() && !state.selected_all_mouse_lock) {
		stb_textedit_drag(&input_data, &state.edit_state, x, y);
		cursor_anim_reset(state);
		state.cursor_follow = true;
	}

	// dragging rather than down since claiming the press makes is_mouse_down false
	if (!keys::is_mouse_dragging())
		state.selected_all_mouse_lock = false;
}

void ui::helpers::text_input::handle_text_input_event(
	TextInputData& input_data, TextInputStateInternal& state, const SDL_Event& event
) {
	input_data.Stb = &state.edit_state; // see handle_mouse

	switch (event.type) {
		case SDL_EVENT_TEXT_INPUT: {
			if (input_data.read_only)
				break;

			// the fork's utf-8 aware input. unlike paste it respects single line and insert mode
			const char* text = event.text.text;
			stb_textedit_text(&input_data, &state.edit_state, text, static_cast<int>(strlen(text)));

			state.composition.clear();
			state.cursor_follow = true;
			cursor_anim_reset(state);
			break;
		}

		case SDL_EVENT_KEY_DOWN: {
			SDL_Scancode scan = event.key.scancode;

			// the event's modifiers rather than SDL_GetModState(), ctrl might've been released by the time the event is
			// handled
			SDL_Keymod mod = event.key.mod;

			bool ctrl = (mod & SDL_KMOD_CTRL) != 0u;
			bool shift = (mod & SDL_KMOD_SHIFT) != 0u;
			bool alt = (mod & SDL_KMOD_ALT) != 0u;
			bool super = (mod & SDL_KMOD_GUI) != 0u;

#ifdef __APPLE__
			bool is_shortcut = super; // cmd
			bool word_move = alt;
#else
			bool is_shortcut = ctrl;
			bool word_move = ctrl;
#endif

			// movement first since it shares ctrl with the clipboard shortcuts on windows/linux
			if (int key = translate_key(scan, ctrl, shift, alt, super)) {
				if (!input_data.read_only || !key_edits_text(key)) {
					int base = key & ~STB_TEXTEDIT_K_SHIFT;
					bool deleting = base == STB_TEXTEDIT_K_BACKSPACE || base == STB_TEXTEDIT_K_DELETE;

					// stb has no delete word key, so select the word and delete that (same as imgui)
					if (deleting && !has_selection(state.edit_state)) {
						if (word_move) {
							stb_textedit_key(
								&input_data,
								&state.edit_state,
								(base == STB_TEXTEDIT_K_BACKSPACE ? STB_TEXTEDIT_K_WORDLEFT
							                                      : STB_TEXTEDIT_K_WORDRIGHT) |
									STB_TEXTEDIT_K_SHIFT
							);
						}
#ifdef __APPLE__
						else if (ctrl && !alt && !super) {
							// mac: ctrl+backspace deletes to the start of the line
							if (base == STB_TEXTEDIT_K_BACKSPACE)
								stb_textedit_key(
									&input_data, &state.edit_state, STB_TEXTEDIT_K_LINESTART | STB_TEXTEDIT_K_SHIFT
								);
						}
#endif
					}

					stb_textedit_key(&input_data, &state.edit_state, key);
				}

				state.cursor_follow = true;
				cursor_anim_reset(state);
				keys::on_key_press_handled(scan);
				break;
			}

			if (is_shortcut) {
				switch (scan) {
					case SDL_SCANCODE_X:
					case SDL_SCANCODE_C: {
						if (has_selection(state.edit_state)) {
							copy_selection_to_clipboard(input_data, state.edit_state);

							if (scan == SDL_SCANCODE_X && !input_data.read_only) {
								stb_textedit_cut(&input_data, &state.edit_state);
								state.cursor_follow = true;
							}
						}
						break;
					}

					case SDL_SCANCODE_V: {
						if (input_data.read_only)
							break;

						if (char* clipboard_text = SDL_GetClipboardText()) {
							stb_textedit_paste(
								&input_data, &state.edit_state, clipboard_text, static_cast<int>(strlen(clipboard_text))
							);
							SDL_free(clipboard_text);
							state.cursor_follow = true;
						}
						break;
					}

					case SDL_SCANCODE_A: {
						select_all(&input_data, &state.edit_state);
						state.selected_all_mouse_lock = true;
						break;
					}

					case SDL_SCANCODE_Z: {
						if (input_data.read_only)
							break;

						// ctrl/cmd + shift + z is redo on every platform that supports it
						stb_textedit_key(
							&input_data, &state.edit_state, shift ? STB_TEXTEDIT_K_REDO : STB_TEXTEDIT_K_UNDO
						);
						state.cursor_follow = true;
						break;
					}

					case SDL_SCANCODE_Y: {
						if (input_data.read_only)
							break;

						stb_textedit_key(&input_data, &state.edit_state, STB_TEXTEDIT_K_REDO);
						state.cursor_follow = true;
						break;
					}

					default:
						// not a text-editing shortcut, leave it for the rest of the ui
						return;
				}

				cursor_anim_reset(state);
				keys::on_key_press_handled(scan);
			}
			break;
		}

		case SDL_EVENT_TEXT_EDITING: {
			if (input_data.read_only)
				break;

			state.composition = event.edit.text;
			state.ime_cursor = event.edit.start;
			state.ime_selection_len = event.edit.length;
			cursor_anim_reset(state);
			break;
		}

		default:
			break;
	}

	stb_textedit_clamp(&input_data, &state.edit_state);
}

ui::helpers::text_input::TextInputStateInternal& ui::helpers::text_input::add_text_edit(
	const std::string& id, TextInputData& input_data
) {
	auto it = text_input_map.find(id);
	if (it == text_input_map.end()) {
		TextInputStateInternal new_state;
		stb_textedit_initialize_state(&new_state.edit_state, 1);
		it = text_input_map.emplace(id, std::move(new_state)).first;
	}
	it->second.edit_state.single_line = input_data.multiline ? 0 : 1;

	// TextInputData is rebuilt every frame so this needs re-pointing each time
	input_data.Stb = &it->second.edit_state;

	return it->second;
}

bool ui::helpers::text_input::has_text_edit(const std::string& id) {
	return text_input_map.contains(id);
}

void ui::helpers::text_input::remove_text_edit(const std::string& id) {
	text_input_map.erase(id);
}

bool ui::helpers::text_input::has_active_text_edit(const std::string& id) {
	auto it = text_input_map.find(id);
	return it != text_input_map.end() && it->second.active;
}

void ui::helpers::text_input::update_ime_area(
	SDL_Window* window, const TextInputStateInternal& state, const render::Font& font
) {
	if (!state.active)
		return;

	// caret rect so the ime window opens next to it
	SDL_Rect rect = {
		state.last_cursor_screen_pos.x,
		state.last_cursor_screen_pos.y,
		1,
		font.height(),
	};

	SDL_SetTextInputArea(window, &rect, 0);
}

void ui::helpers::text_input::render_text(
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
) {
	render::push_clip_rect(clip_rect, true);

	const std::string& display_text = *input_data.text;

	// scrolling
	// only scroll when the caret would leave the visible range, a chunk at a time
	if (input_data.multiline) {
		state.scroll_x = 0.f;
		state.cursor_follow = false;
	}
	else {
		float cursor_offset = get_cursor_x(input_data, state.edit_state.cursor, gfx::Point(0, 0));
		auto visible_width = static_cast<float>(clip_rect.w);
		float text_width = measure(input_data.font, display_text, 0, static_cast<int>(display_text.size()));

		// leave room for the caret itself so it isn't clipped when sitting at the very end of the text
		float max_scroll = std::max(0.f, (text_width + CURSOR_WIDTH) - visible_width);

		if (state.active && state.cursor_follow) {
			float scroll_chunk = visible_width * SCROLL_CHUNK_FRACTION;

			if (cursor_offset < state.scroll_x)
				state.scroll_x = std::max(0.f, cursor_offset - scroll_chunk);
			else if (cursor_offset - visible_width >= state.scroll_x)
				state.scroll_x = cursor_offset - visible_width + scroll_chunk;

			state.cursor_follow = false;
		}

		// deleting text can leave us scrolled past the end
		state.scroll_x = std::clamp(state.scroll_x, 0.f, max_scroll);
	}

	text_pos.x -= static_cast<int>(state.scroll_x);

	if (display_text.empty() && !state.active && !placeholder.empty()) {
		render::text(text_pos, placeholder_color, placeholder, input_data.font);
		render::pop_clip_rect();
		return;
	}

	// render selection
	if (has_selection(state.edit_state)) {
		int sel_start = state.edit_state.select_start;
		int sel_end = state.edit_state.select_end;
		if (sel_start > sel_end)
			std::swap(sel_start, sel_end);

		if (input_data.multiline) {
			int line_start = 0;
			int line = 0;
			int text_length = static_cast<int>(display_text.length());

			while (line_start <= text_length) {
				size_t newline = display_text.find('\n', line_start);
				int line_end = newline == std::string::npos ? text_length : static_cast<int>(newline);
				int start = std::clamp(sel_start, line_start, line_end);
				int end = std::clamp(sel_end, line_start, line_end);
				bool newline_selected = line_end < text_length && sel_start <= line_end && sel_end > line_end;

				if (end > start || newline_selected) {
					float x1 = text_pos.x + measure(input_data.font, display_text, line_start, start - line_start);
					float x2 = text_pos.x + measure(input_data.font, display_text, line_start, end - line_start);
					int width = std::max(static_cast<int>(x2 - x1), newline_selected ? 4 : 0);

					if (width > 0) {
						render::rect_filled(
							gfx::Rect(
								static_cast<int>(x1),
								text_pos.y + (line * input_data.font.height()),
								width,
								input_data.font.height()
							),
							selection_colour
						);
					}
				}

				if (newline == std::string::npos)
					break;

				line_start = line_end + 1;
				line++;
			}
		}
		else {
			float x1 = get_cursor_x(input_data, sel_start, text_pos);
			float x2 = get_cursor_x(input_data, sel_end, text_pos);

			gfx::Rect selection_rect(
				static_cast<int>(x1), text_pos.y, static_cast<int>(x2 - x1), input_data.font.height()
			);

			if (selection_rect.w > 0 && selection_rect.h > 0)
				render::rect_filled(selection_rect, selection_colour);
		}
	}

	render::text(text_pos, text_color, display_text, input_data.font);

	// ime composition
	if (state.active && !state.composition.empty()) {
		float base_comp_x = get_cursor_x(input_data, state.edit_state.cursor, text_pos);
		gfx::Point comp_pos = { static_cast<int>(base_comp_x), text_pos.y };
		gfx::Size comp_size = input_data.font.calc_size(state.composition);
		gfx::Rect comp_bg_rect(comp_pos.x, comp_pos.y, comp_size.w, comp_size.h);

		render::rect_filled(comp_bg_rect, composition_bg_color);
		render::text(comp_pos, composition_text_color, state.composition, input_data.font);
		render::line(
			{ comp_pos.x, comp_pos.y + comp_size.h },
			{ comp_pos.x + comp_size.w, comp_pos.y + comp_size.h },
			composition_text_color
		);

		if (state.ime_cursor >= 0) {
			float ime_cursor_x_offset = measure(
				input_data.font, state.composition, 0, std::min(state.ime_cursor, (int)state.composition.size())
			);
			render::line(
				{ comp_pos.x + (int)ime_cursor_x_offset, comp_pos.y },
				{ comp_pos.x + (int)ime_cursor_x_offset, comp_pos.y + input_data.font.height() },
				composition_text_color,
				false,
				1.f
			);
		}
	}

	// render cursor
	if (state.active) {
		auto cursor_pos = get_text_position(input_data, state.edit_state.cursor);
		auto cursor_x = static_cast<int>(text_pos.x + cursor_pos.x);
		auto cursor_y = text_pos.y + (cursor_pos.line * input_data.font.height());
		state.last_cursor_screen_pos = { cursor_x, cursor_y };

		state.cursor_anim += render::frametime;

		// solid right after an action (cursor_anim starts negative), then blinks
		bool cursor_visible =
			state.cursor_anim <= 0.f || std::fmod(state.cursor_anim, CURSOR_BLINK_PERIOD) <= CURSOR_BLINK_ON;

		if (cursor_visible) {
			gfx::Point p1(cursor_x, cursor_y);
			gfx::Point p2(cursor_x, cursor_y + input_data.font.height());

			auto current_clip = render::get_clip_rect();
			if (p1.x >= current_clip.x && p1.x <= current_clip.x + current_clip.w)
				render::line(p1, p2, text_color, false, 1.f);
		}
	}

	render::pop_clip_rect();
}
