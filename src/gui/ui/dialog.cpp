#include "ui.h"
#include "keys.h"
#include "../render/render.h"

const int DIALOG_WIDTH = 330;
const gfx::Size DIALOG_PADDING(24, 22);
const int DIALOG_ELEMENT_GAP = 10;
const float DIALOG_ROUNDING = 10.f;
const int DIALOG_BACKDROP_ALPHA = 170;
const gfx::Color DIALOG_BACKGROUND_COLOR(0, 0, 0, 255);
const gfx::Color DIALOG_BORDER_COLOR(70, 70, 70, 255);

const float DIALOG_SHAKE_DURATION = 0.35f;
const float DIALOG_SHAKE_SPEED = 45.f;
const float DIALOG_SHAKE_DISTANCE = 2.f;

namespace {
	std::optional<ui::dialog::Options> current;

	ui::Container container;
	ui::AnimationState anim(20.f);

	// where the panel was last drawn, kept so it stays put while the dialog fades back out
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

	// the elements are gone by the time the callback runs (it can open another dialog), so take a copy first
	void run_and_close(const std::function<void()>& callback) {
		auto to_run = callback;

		ui::dialog::close();

		if (to_run)
			to_run();
	}

	void confirm() {
		if (!current)
			return;

		run_and_close(current->on_confirm);
	}

	void cancel() {
		if (!current)
			return;

		run_and_close(current->on_cancel);
	}

	// the panel wraps whatever got added, so measure it rather than guessing a height
	std::optional<gfx::Rect> get_content_rect() {
		std::optional<gfx::Rect> content;

		for (const auto& id : container.current_element_ids) {
			const auto& element = container.elements[id].element;

			if (!content) {
				content = element->rect;
				continue;
			}

			int x = std::min(content->x, element->rect.x);
			int y = std::min(content->y, element->rect.y);

			content = gfx::Rect(
				x, y, std::max(content->x2(), element->rect.x2()) - x, std::max(content->y2(), element->rect.y2()) - y
			);
		}

		return content;
	}
}

void ui::dialog::open(Options options) {
	current = std::move(options);
	shake_time_left = 0.f;
}

void ui::dialog::close() {
	current.reset();
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
			.body = body,
			.detail = detail,
			.confirm_text = confirm_text,
			.confirm_color = gfx::Color(255, 80, 80, 255),
			.on_confirm = std::move(on_confirm),
		}
	);
}

void ui::dialog::build(SDL_Window* window, const gfx::Rect& screen_rect) {
	// reset every frame even when closed, otherwise the old elements never go stale and never fade out
	gfx::Rect container_rect(
		screen_rect.x + ((screen_rect.w - DIALOG_WIDTH) / 2), screen_rect.y, DIALOG_WIDTH, screen_rect.h
	);

	reset_container(container, window, container_rect, DIALOG_ELEMENT_GAP, Padding(DIALOG_PADDING.h, DIALOG_PADDING.w));

	if (!current) {
		if (anim.current < 0.01f)
			panel_rect.reset();

		return;
	}

	if (keys::is_key_pressed(SDL_SCANCODE_ESCAPE)) {
		keys::on_key_press_handled(SDL_SCANCODE_ESCAPE);
		cancel();
		return;
	}

	add_text("dialog title", container, current->title, gfx::Color::white(), fonts::dejavu);

	if (!current->body.empty())
		add_text("dialog body", container, current->body, gfx::Color::white(150), fonts::dejavu);

	if (!current->detail.empty())
		add_text(
			"dialog detail", container, current->detail, gfx::Color::white(105), fonts::dejavu(fonts::size::SMALL)
		);

	add_spacing(container, 4);

	add_button(
		"dialog confirm button",
		container,
		current->confirm_text,
		fonts::dejavu,
		[] {
			confirm();
		},
		current->confirm_color
	);

	set_next_same_line(container);

	add_button("dialog cancel button", container, current->cancel_text, fonts::dejavu, [] {
		cancel();
	});

	center_elements_in_container(container);

	if (auto content = get_content_rect()) {
		panel_rect = gfx::Rect(
			content->x - DIALOG_PADDING.w,
			content->y - DIALOG_PADDING.h,
			content->w + (DIALOG_PADDING.w * 2),
			content->h + (DIALOG_PADDING.h * 2)
		);
	}

	// the layout above runs fresh every frame, so this offsets rather than accumulates
	if (int shake_offset = get_shake_offset()) {
		for (const auto& id : container.current_element_ids) {
			auto& element = container.elements[id].element;

			element->rect.x += shake_offset;
			element->orig_rect.x = element->rect.x;
		}

		if (panel_rect)
			panel_rect->x += shake_offset;
	}
}

bool ui::dialog::update_input() {
	// the buttons get the click first, anything they took won't still be down
	bool updated = update_container_input(container);

	if (is_open() && keys::is_mouse_down() && (!panel_rect || !panel_rect->contains(keys::mouse_pos))) {
		keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

		// clicking away doesn't dismiss it, shake to point back at the buttons
		shake_time_left = DIALOG_SHAKE_DURATION;
		updated = true;
	}

	return updated;
}

bool ui::dialog::update_frame(float delta_time) {
	anim.set_goal(is_open() ? 1.f : 0.f);

	bool updated = anim.update(delta_time);
	updated |= update_container_frame(container, delta_time);

	if (shake_time_left > 0.f) {
		shake_time_left = std::max(shake_time_left - delta_time, 0.f);
		updated = true;
	}

	return updated;
}

void ui::dialog::render() {
	if (anim.current < 0.01f)
		return;

	// dim everything behind it
	render::rect_filled(
		gfx::Rect(gfx::Point(0, 0), render::window_size), gfx::Color::black(anim.current * DIALOG_BACKDROP_ALPHA)
	);

	if (panel_rect) {
		render::rounded_rect_filled(
			*panel_rect, gfx::Color(DIALOG_BACKGROUND_COLOR, anim.current * 255), DIALOG_ROUNDING
		);
		render::rounded_rect_stroke(*panel_rect, gfx::Color(DIALOG_BORDER_COLOR, anim.current * 255), DIALOG_ROUNDING);
	}

	render_container(container);
}
