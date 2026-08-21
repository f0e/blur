#include "ui.h"
#include "keys.h"
#include "../render/render.h"

const gfx::Size DIALOG_PADDING(24, 22);
const int DIALOG_ELEMENT_GAP = 10;
const int DIALOG_SCREEN_MARGIN = 24;
const int DIALOG_FOOTER_GAP = 12;
const float DIALOG_ROUNDING = 10.f;
const int DIALOG_BACKDROP_ALPHA = 170;
const float DIALOG_FADED_OUT = 0.01f;
const gfx::Color DIALOG_BACKGROUND_COLOR(0, 0, 0, 255);
const gfx::Color DIALOG_BORDER_COLOR(70, 70, 70, 255);

const float DIALOG_SHAKE_DURATION = 0.35f;
const float DIALOG_SHAKE_SPEED = 45.f;
const float DIALOG_SHAKE_DISTANCE = 2.f;

namespace {
	std::optional<ui::dialog::Options> current;

	ui::Container container;
	ui::Container button_container;
	ui::AnimationState anim(20.f);

	// the panel's last built bounds, kept alive past the close so the dialog has something to fade out as
	std::optional<gfx::Rect> panel_rect;

	float shake_time_left = 0.f;

	// a sine that dies out over the shake, so it settles back where it started
	int get_shake_offset() {
		if (shake_time_left <= 0.f)
			return 0;

		float remaining = shake_time_left / DIALOG_SHAKE_DURATION;
		float elapsed = DIALOG_SHAKE_DURATION - shake_time_left;

		return std::lround(std::sin(elapsed * DIALOG_SHAKE_SPEED) * DIALOG_SHAKE_DISTANCE * remaining);
	}

	// both of these copy the callback out before closing: closing destroys the options it lives in, and the
	// callback is free to open another dialog on top

	void confirm() {
		if (!current)
			return;

		auto callback = current->on_confirm;

		if (current->close_on_confirm)
			ui::dialog::close();

		if (callback)
			callback();
	}

	void cancel() {
		if (!current)
			return;

		auto callback = current->on_cancel;

		ui::dialog::close();

		if (callback)
			callback();
	}

	int available_width(const gfx::Rect& screen_rect) {
		return std::max(screen_rect.w - (DIALOG_SCREEN_MARGIN * 2), 1);
	}

	int available_height(const gfx::Rect& screen_rect) {
		return std::max(screen_rect.h - (DIALOG_SCREEN_MARGIN * 2), 1);
	}

	// the single rule for where the dialog sits: centred in whatever the screen margin leaves it
	gfx::Rect centered_panel(const gfx::Rect& screen_rect, const gfx::Size& size) {
		return {
			screen_rect.x + ((screen_rect.w - size.w) / 2),
			screen_rect.y + DIALOG_SCREEN_MARGIN + ((available_height(screen_rect) - size.h) / 2),
			size.w,
			size.h,
		};
	}

	int get_container_content_height(const ui::Container& target) {
		int height = target.current_position.y - target.get_usable_rect().y;
		return std::max(height - target.last_margin_bottom, 0);
	}

	// moves a container and everything in it, stale elements included, so a whole dialog can be repositioned
	// after the fact without relaying it out
	void shift_container(ui::Container& target, const gfx::Point& amount) {
		target.rect += amount;
		target.current_position += amount;
		target.same_line_bottom += amount.y;

		for (auto& [id, animated_element] : target.elements) {
			animated_element.element->rect += amount;
			animated_element.element->orig_rect += amount;
		}
	}
}

void ui::dialog::open(Options options) {
	current = std::move(options);
	shake_time_left = 0.f;
	container.scroll_y = 0.f;
	container.scroll_speed_y = 0.f;
}

void ui::dialog::close() {
	current.reset();
	reset_active_element();
}

bool ui::dialog::is_open() {
	return current.has_value();
}

void ui::dialog::confirm_destructive(
	const std::string& title,
	const std::string& body,
	const std::string& confirm_text,
	std::function<void()> on_confirm,
	const std::string& detail
) {
	open(
		{
			.title = title,
			.content =
				[body, detail](ui::Container& container) {
					if (!body.empty())
						add_body(container, "dialog body", body);

					if (!detail.empty())
						add_detail(container, "dialog detail", detail);
				},
			.action_required = true,
			.confirm_text = confirm_text,
			.confirm_color = gfx::Color(255, 80, 80, 255),
			.on_confirm = std::move(on_confirm),
		}
	);
}

void ui::dialog::add_body(Container& container, const std::string& id, const std::string& text) {
	add_text(id, container, text, gfx::Color::white(150), fonts::dejavu);
}

void ui::dialog::add_detail(Container& container, const std::string& id, const std::string& text) {
	add_text(id, container, text, gfx::Color::white(105), fonts::dejavu(fonts::size::SMALL));
}

void ui::dialog::add_heading(Container& container, const std::string& id, const std::string& text) {
	add_text(id, container, text, gfx::Color::white(165), fonts::dejavu);
}

void ui::dialog::add_field(
	Container& container,
	const std::string& id,
	const std::string& label,
	const std::string& text,
	const render::Font& font
) {
	if (!label.empty()) {
		// a label and its value read as one group. the element-type padding still leaves a small gap between them
		add_text(std::format("{} label", id), container, label, gfx::Color::white(190), fonts::dejavu);
		add_spacing(container, -DIALOG_ELEMENT_GAP);
	}

	add_selectable_text(id, container, text, font);
}

void ui::dialog::build(SDL_Window* window, const gfx::Rect& screen_rect) {
	// handled here rather than in update_input so the dialog closes and starts fading within the same frame
	if (current && keys::is_key_pressed(SDL_SCANCODE_ESCAPE)) {
		keys::on_key_press_handled(SDL_SCANCODE_ESCAPE);
		cancel();
	}

	// the containers are reset even when closed, otherwise the old elements never go stale and never fade out
	if (!current) {
		reset_container(container, window, container.rect, DIALOG_ELEMENT_GAP);
		reset_container(button_container, window, button_container.rect, DIALOG_ELEMENT_GAP);

		if (anim.current < DIALOG_FADED_OUT) {
			panel_rect.reset();
			return;
		}

		// nothing relays out once the dialog is gone, so a window resize would strand the fading panel wherever it
		// happened to close. re-centre it and carry its contents along, at the size it was last built at.
		if (panel_rect) {
			gfx::Point shift = centered_panel(screen_rect, panel_rect->size()).origin() - panel_rect->origin();

			if (shift != gfx::Point(0, 0)) {
				*panel_rect += shift;
				shift_container(container, shift);
				shift_container(button_container, shift);
			}
		}

		return;
	}

	int button_height = ui::button_height(fonts::dejavu);
	int dialog_width = std::min(current->width, available_width(screen_rect));
	int footer_reserve = DIALOG_FOOTER_GAP + button_height + DIALOG_PADDING.h;

	// the content is laid out against the full height available, then the panel shrinks onto what it actually used
	gfx::Rect content_rect = centered_panel(screen_rect, gfx::Size(dialog_width, available_height(screen_rect)));

	reset_container(
		container,
		window,
		content_rect,
		DIALOG_ELEMENT_GAP,
		Padding(DIALOG_PADDING.h, DIALOG_PADDING.w, footer_reserve, DIALOG_PADDING.w)
	);

	add_text("dialog title", container, current->title, gfx::Color::white(), fonts::dejavu);

	if (current->content)
		current->content(container);

	int natural_height = DIALOG_PADDING.h + get_container_content_height(container) + footer_reserve;
	panel_rect = centered_panel(screen_rect, gfx::Size(dialog_width, std::min(natural_height, content_rect.h)));

	shift_container(container, gfx::Point(0, panel_rect->y - content_rect.y));
	container.rect = *panel_rect; // anything taller than the panel scrolls inside it

	reset_container(
		button_container,
		window,
		gfx::Rect(panel_rect->x, panel_rect->y2() - DIALOG_PADDING.h - button_height, panel_rect->w, button_height),
		DIALOG_ELEMENT_GAP
	);
	add_button(
		"dialog confirm button",
		button_container,
		current->confirm_text,
		fonts::dejavu,
		[] {
			confirm();
		},
		current->confirm_color,
		current->confirm_icon
	);

	set_next_same_line(button_container);

	add_button("dialog cancel button", button_container, current->cancel_text, fonts::dejavu, [] {
		cancel();
	});

	center_elements_in_container(button_container, true, false);

	// the layout above runs fresh every frame, so this offsets rather than accumulates
	if (int shake_offset = get_shake_offset()) {
		gfx::Point offset(shake_offset, 0);

		*panel_rect += offset;
		shift_container(container, offset);
		shift_container(button_container, offset);
	}
}

bool ui::dialog::update_input() {
	// Release a selected text field before dispatching a click elsewhere, so the same click can activate a button.
	if (keys::is_mouse_pressed()) {
		if (auto* active = get_active_element(); active && active->element->type == ElementType::TEXT_INPUT &&
		                                         !active->element->rect.contains(keys::mouse_pos))
		{
			reset_active_element();
		}
	}

	if (is_open() && keys::is_mouse_down() && (!panel_rect || !panel_rect->contains(keys::mouse_pos))) {
		keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

		if (current->action_required) {
			// Required decisions stay open and point back at their actions.
			shake_time_left = DIALOG_SHAKE_DURATION;
		}
		else {
			cancel();
		}
		return true;
	}

	bool updated = update_container_input(button_container);
	updated |= update_container_input(container);
	return updated;
}

bool ui::dialog::update_frame(float delta_time) {
	anim.set_goal(is_open() ? 1.f : 0.f);

	bool updated = anim.update(delta_time);
	updated |= update_container_frame(container, delta_time);
	updated |= update_container_frame(button_container, delta_time);

	if (shake_time_left > 0.f) {
		shake_time_left = std::max(shake_time_left - delta_time, 0.f);
		updated = true;
	}

	return updated;
}

void ui::dialog::render() {
	if (anim.current < DIALOG_FADED_OUT || !panel_rect)
		return;

	// dim everything behind it
	render::rect_filled(
		gfx::Rect(gfx::Point(0, 0), render::window_size), gfx::Color::black(anim.current * DIALOG_BACKDROP_ALPHA)
	);

	render::rounded_rect_filled(*panel_rect, gfx::Color(DIALOG_BACKGROUND_COLOR, anim.current * 255), DIALOG_ROUNDING);
	render::rounded_rect_stroke(*panel_rect, gfx::Color(DIALOG_BORDER_COLOR, anim.current * 255), DIALOG_ROUNDING);

	// nothing in the dialog draws outside its panel
	render::push_clip_rect(panel_rect->shrink(1), true);
	render_container(container);
	render_container(button_container);
	render::pop_clip_rect();
}
