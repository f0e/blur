#include "text_input.h"
#include "../keys.h"
#include "../../render/render.h"

// scroll in chunks rather than by a pixel at a time, so the text doesn't jitter as you type at the edge
constexpr float SCROLL_CHUNK_FRACTION = 0.25f;

constexpr float CURSOR_BLINK_PERIOD = 1.2f; // seconds for a full blink cycle
constexpr float CURSOR_BLINK_ON = 0.8f;     // how much of that cycle the caret is visible for
constexpr float CURSOR_BLINK_SOLID = -0.3f; // grace period after an action where the caret stays solid
constexpr float CURSOR_WIDTH = 1.f;

namespace {
	// --- UTF-8 -------------------------------------------------------------------------------------------
	// decoded by hand rather than via imgui's ImTextCharFromUtf8: that lives in imgui_internal.h, which
	// defines its own IMSTB_TEXTEDIT_* configuration and would fight ours if included here.

	int utf8_seq_len(unsigned char lead) {
		if (lead < 0x80)
			return 1;
		if ((lead & 0xE0) == 0xC0)
			return 2;
		if ((lead & 0xF0) == 0xE0)
			return 3;
		if ((lead & 0xF8) == 0xF0)
			return 4;
		return 1; // invalid lead byte, treat as one byte so we always make progress
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

		static constexpr std::array<unsigned int, 5> lead_mask = { 0, 0x7F, 0x1F, 0x0F, 0x07 };
		unsigned int c = lead & lead_mask[len];
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
			return len + 1; // matches imgui: signals "past the end" rather than clamping

		int next = idx + utf8_seq_len(static_cast<unsigned char>((*str->text)[idx]));
		return std::min(next, len);
	}

	int textedit_getprevcharindex(IMSTB_TEXTEDIT_STRING* str, int idx) {
		if (idx <= 0)
			return -1;

		int i = idx - 1;
		// walk back over continuation bytes. a codepoint is at most 4 bytes, so cap the walk to keep a
		// malformed sequence from running to the start of the string
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
	// --- measurement -------------------------------------------------------------------------------------
	// render::Font::calc_size truncates to int, which is fine for layout but not here: stb sums per-character
	// widths and compares them against whole-run widths, so the two have to agree at float precision or the
	// caret drifts away from the glyphs on longer strings.

	float measure(const render::Font& font, const char* begin, const char* end) {
		if (!font || begin >= end)
			return 0.f;

		return font.im_font()->CalcTextSizeA(font.size(), FLT_MAX, 0.f, begin, end).x;
	}

	float measure(const render::Font& font, const std::string& text, int offset, int count) {
		const char* data = text.data();
		return measure(font, data + offset, data + offset + count);
	}

	// --- stb_textedit callbacks --------------------------------------------------------------------------

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

		// measure the whole codepoint, not the single byte the old implementation used
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

	// returns the number of characters actually inserted (imgui's fork changed this from a 0/1 bool so that
	// partial insertion works)
	int textedit_insertchars(IMSTB_TEXTEDIT_STRING* str, int i, const IMSTB_TEXTEDIT_CHARTYPE* c, int n) {
		if (!str || !str->text || str->read_only || n <= 0)
			return 0;

		i = std::clamp(i, 0, string_len(str));
		str->text->insert(i, std::string(c, n));

		if (str->on_change)
			(*str->on_change)(*str->text);

		return n;
	}

	// --- word movement -----------------------------------------------------------------------------------
	// ported from imgui's InputTextEx so double-click and ctrl+arrow agree with what the OS does

	bool is_blank(unsigned int c) {
		return c == ' ' || c == '\t' || c == 0x3000; // includes the ideographic space
	}

	bool is_separator(unsigned int c) {
		static constexpr std::array<unsigned int, 29> separators = {
			',', 0x3001, '.', 0x3002, ';', 0xFF1B, '(',  0xFF08, ')', 0xFF09, '{',    0xFF5B, '}',  0xFF5D, '[', 0x300C,
			']', 0x300D, '|', 0xFF5C, '!', 0xFF01, '\\', 0xFFE5, '/', 0x30FB, 0xFF0F, '\n',   '\r',
		};

		return std::ranges::find(separators, c) != separators.end();
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

// note: STB_TEXTEDIT_KEYTOTEXT is deliberately left undefined. the fork routes character input through
// stb_textedit_text() instead, which is what makes multi-byte input work.

// sentinel key values, deliberately not overlapping SDL scancodes. the old mapping reused scancodes and or'd
// in a ctrl bit that the event handler never actually set, which silently broke undo/redo.
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

#define IMSTB_TEXTEDIT_IMPLEMENTATION
#include <imstb_textedit.h>

namespace {
	// translates an SDL key event into an stb key, or 0 if the key isn't a text-editing key.
	// shortcuts (cut/copy/paste/select all/undo/redo) are handled separately in handle_text_input_event.
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
				key = word_move ? STB_TEXTEDIT_K_WORDLEFT : (doc_move ? STB_TEXTEDIT_K_LINESTART : STB_TEXTEDIT_K_LEFT);
				break;
			case SDL_SCANCODE_RIGHT:
				key = word_move ? STB_TEXTEDIT_K_WORDRIGHT : (doc_move ? STB_TEXTEDIT_K_LINEEND : STB_TEXTEDIT_K_RIGHT);
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
	if (!input_data.text || !input_data.font)
		return static_cast<float>(text_start_pos.x);

	cursor_pos = std::clamp(cursor_pos, 0, static_cast<int>(input_data.text->length()));

	return static_cast<float>(text_start_pos.x) + measure(input_data.font, *input_data.text, 0, cursor_pos);
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
	// the fork dereferences str->Stb, and element data can be rebuilt between frames, so keep it fresh here
	// rather than relying on it having been set once at creation
	input_data.Stb = &state.edit_state;

	auto x = static_cast<float>(text_relative_pos.x);
	auto y = static_cast<float>(text_relative_pos.y);

	// the click count stays set until the next press, so gate on a fresh press or we'd re-select the word on
	// every frame the mouse merely hovers after a double click
	if (pressed && hovered && click_count >= 2 && !shift) {
		stb_textedit_click(&input_data, &state.edit_state, x, y);

		// alternate word / line selection as the click count keeps going up, matching imgui
		if ((click_count - 2) % 2 == 0) {
			// double click: select the word under the cursor. always uses the mac-style word advance, since
			// selecting up to the end of the word is what every platform does on double click
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

	// is_mouse_dragging rather than is_mouse_down: claiming the press moves the button from "pressed" to
	// "held", so is_mouse_down goes false while the button is still physically down
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

			// stb_textedit_text() rather than _paste(): it's the fork's UTF-8 aware entry point for character
			// input, and unlike paste it respects single-line mode and insert mode
			const char* text = event.text.text;
			stb_textedit_text(&input_data, &state.edit_state, text, static_cast<int>(strlen(text)));

			state.composition.clear();
			state.cursor_follow = true;
			cursor_anim_reset(state);
			break;
		}

		case SDL_EVENT_KEY_DOWN: {
			SDL_Scancode scan = event.key.scancode;

			// the modifiers recorded on the event, not SDL_GetModState(). events sit in text_event_queue until
			// the ui updates, so by the time we get here the user may already have let go of ctrl - which made
			// shortcuts (ctrl+a, ctrl+z, ...) silently no-op depending on frame timing
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

			// movement and deletion first: on windows/linux these share the ctrl modifier with the clipboard
			// shortcuts below, and checking shortcuts first swallowed every ctrl+arrow / ctrl+home
			if (int key = translate_key(scan, ctrl, shift, alt, super)) {
				if (!input_data.read_only || !key_edits_text(key)) {
					int base = key & ~STB_TEXTEDIT_K_SHIFT;
					bool deleting = base == STB_TEXTEDIT_K_BACKSPACE || base == STB_TEXTEDIT_K_DELETE;

					// stb has no "delete word" key, so select the word first and let the delete take the
					// selection out - this is how imgui does ctrl+backspace too
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
		new_state.edit_state.single_line = 1;
		it = text_input_map.emplace(id, std::move(new_state)).first;
	}

	// the fork dereferences str->Stb, and TextInputData is rebuilt each frame by the element, so this has to be
	// re-pointed every time rather than only on creation
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

	// a thin rect at the caret, so the candidate window opens next to what's being typed rather than at the
	// start of the field
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

	// --- Scrolling ---
	// sticky: only moves when the caret would leave the visible range, and then by a chunk at a time. the old
	// version recomputed the offset from the caret every frame, so the text jumped around while typing
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

	// deleting text (or the field growing) can leave us scrolled past the end, which would show a blank gap
	state.scroll_x = std::clamp(state.scroll_x, 0.f, max_scroll);

	text_pos.x -= static_cast<int>(state.scroll_x);

	if (display_text.empty() && !state.active && !placeholder.empty()) {
		render::text(text_pos, placeholder_color, placeholder, input_data.font);
		render::pop_clip_rect();
		return;
	}

	// --- Render Selection ---
	if (has_selection(state.edit_state)) {
		int sel_start = state.edit_state.select_start;
		int sel_end = state.edit_state.select_end;
		if (sel_start > sel_end)
			std::swap(sel_start, sel_end);

		float x1 = get_cursor_x(input_data, sel_start, text_pos);
		float x2 = get_cursor_x(input_data, sel_end, text_pos);

		gfx::Rect selection_rect(static_cast<int>(x1), text_pos.y, static_cast<int>(x2 - x1), input_data.font.height());

		if (selection_rect.w > 0 && selection_rect.h > 0)
			render::rect_filled(selection_rect, selection_colour);
	}

	render::text(text_pos, text_color, display_text, input_data.font);

	// --- Render IME Composition ---
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

	// --- Render Cursor ---
	if (state.active) {
		auto cursor_x = static_cast<int>(get_cursor_x(input_data, state.edit_state.cursor, text_pos));
		state.last_cursor_screen_pos = { cursor_x, text_pos.y };

		state.cursor_anim += render::frametime;

		// solid right after an action (cursor_anim starts negative), then blinks
		bool cursor_visible =
			state.cursor_anim <= 0.f || std::fmod(state.cursor_anim, CURSOR_BLINK_PERIOD) <= CURSOR_BLINK_ON;

		if (cursor_visible) {
			gfx::Point p1(cursor_x, text_pos.y);
			gfx::Point p2(cursor_x, text_pos.y + input_data.font.height());

			auto current_clip = render::get_clip_rect();
			if (p1.x >= current_clip.x && p1.x <= current_clip.x + current_clip.w)
				render::line(p1, p2, text_color, false, 1.f);
		}
	}

	render::pop_clip_rect();
}
