#include "../ui.h"
#include "../../render/render.h"
#include "../keys.h"

static const int ENTRY_PADDING = ui::RENDER_HISTORY_ENTRY_PADDING;
static const float ENTRY_ROUNDING = 6.f;

static const int THUMBNAIL_W = 71; // 16:9, matching the height below
static const int THUMBNAIL_H = 40;
static const int THUMBNAIL_GAP = 9;

static const int ACTION_SIZE = ui::RENDER_HISTORY_ACTION_SIZE;
static const int ACTION_GAP = 2;

static const int TITLE_DETAIL_GAP = 3;
static const int MAX_DETAIL_LINES = 2;

namespace {
	const gfx::Color DETAIL_COLOR = gfx::Color::white(120);
	const gfx::Color ERROR_DETAIL_COLOR = { 255, 110, 110, 255 };

	gfx::Rect get_inner_rect(const gfx::Rect& entry_rect) {
		return entry_rect.shrink(ENTRY_PADDING);
	}

	gfx::Rect get_thumbnail_rect(const gfx::Rect& entry_rect) {
		gfx::Rect inner = get_inner_rect(entry_rect);

		return {
			inner.x,
			inner.y + ((inner.h - THUMBNAIL_H) / 2),
			THUMBNAIL_W,
			THUMBNAIL_H,
		};
	}

	int get_actions_width(size_t action_count) {
		if (action_count == 0)
			return 0;

		int count = static_cast<int>(action_count);
		return (count * ACTION_SIZE) + ((count - 1) * ACTION_GAP);
	}

	// action buttons sit in a row against the right edge, centered vertically
	gfx::Rect get_action_rect(const gfx::Rect& entry_rect, size_t index, size_t action_count) {
		gfx::Rect inner = get_inner_rect(entry_rect);
		int offset = static_cast<int>(index) * (ACTION_SIZE + ACTION_GAP);

		return {
			inner.x2() - get_actions_width(action_count) + offset,
			inner.y + ((inner.h - ACTION_SIZE) / 2),
			ACTION_SIZE,
			ACTION_SIZE,
		};
	}

	// the space the title and detail lines get, between the thumbnail and the actions
	int get_text_width(int entry_width, size_t action_count) {
		int width = entry_width - (ENTRY_PADDING * 2) - THUMBNAIL_W - THUMBNAIL_GAP;

		if (action_count > 0)
			width -= get_actions_width(action_count) + THUMBNAIL_GAP;

		return std::max(width, 0);
	}

	size_t action_animation_key(size_t index) {
		return ui::hasher(std::format("action_hover_{}", index));
	}

	// on the way in the row grows out of the history button, on the way out it shrinks back into it
	gfx::Rect get_animated_rect(const ui::RenderHistoryEntryElementData& data, const gfx::Rect& rect, float anim) {
		if (!data.collapse_rect || anim >= 1.f)
			return rect;

		const auto& target = *data.collapse_rect;

		auto lerp_edge = [anim](int from, int to) {
			return static_cast<int>(std::lround(std::lerp(static_cast<float>(to), static_cast<float>(from), anim)));
		};

		return {
			lerp_edge(rect.x, target.x),
			lerp_edge(rect.y, target.y),
			lerp_edge(rect.w, target.w),
			lerp_edge(rect.h, target.h),
		};
	}
}

void ui::render_render_history_entry(const Container& container, const AnimatedElement& element) {
	const auto& entry_data = std::get<RenderHistoryEntryElementData>(element.element->data);

	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;

	gfx::Rect rect = get_animated_rect(entry_data, element.element->rect, anim);

	// the contents go before the box does, so it reads as the row folding away into the button
	float content_anim = anim * anim;

	gfx::Color fill_color = entry_data.error
	                            ? gfx::Color::lerp(gfx::Color(38, 20, 20), gfx::Color(48, 26, 26), hover_anim)
	                            : gfx::Color::lerp(gfx::Color(20, 20, 20), gfx::Color(28, 28, 28), hover_anim);
	gfx::Color stroke_color = entry_data.error ? gfx::Color(120, 60, 60, 110) : gfx::Color::white(45);

	render::rounded_rect_filled(rect, fill_color.adjust_alpha(anim), ENTRY_ROUNDING);
	render::rounded_rect_stroke(rect, stroke_color.adjust_alpha(anim), ENTRY_ROUNDING);

	if (content_anim <= 0.01f)
		return;

	// the contents are laid out for the row's full size, so keep them inside it while it's shrinking away
	render::push_clip_rect(rect, true);

	// thumbnail, or a placeholder while one's still being generated
	gfx::Rect thumbnail_rect = get_thumbnail_rect(rect);

	gfx::Color thumbnail_border = gfx::Color::white(45).adjust_alpha(content_anim);
	gfx::Color thumbnail_inner_border = gfx::Color::white(20).adjust_alpha(content_anim);

	if (entry_data.thumbnail && entry_data.thumbnail->is_valid()) {
		// videos that aren't 16:9 keep their shape inside the thumbnail box
		const auto& texture = *entry_data.thumbnail;

		float aspect_ratio = texture.width() / static_cast<float>(texture.height());

		gfx::Rect image_rect = thumbnail_rect;
		if (aspect_ratio > thumbnail_rect.w / static_cast<float>(thumbnail_rect.h))
			image_rect.h = static_cast<int>(thumbnail_rect.w / aspect_ratio);
		else
			image_rect.w = static_cast<int>(thumbnail_rect.h * aspect_ratio);

		image_rect.x += (thumbnail_rect.w - image_rect.w) / 2;
		image_rect.y += (thumbnail_rect.h - image_rect.h) / 2;

		render::image(image_rect, texture, gfx::Color::white().adjust_alpha(content_anim));
		render::borders(image_rect, thumbnail_border, thumbnail_inner_border);
	}
	else {
		render::rect_filled(thumbnail_rect, gfx::Color::white(12).adjust_alpha(content_anim));
		render::borders(thumbnail_rect, thumbnail_border, thumbnail_inner_border);
	}

	// title above the detail lines, the block centered against the thumbnail
	int text_width = get_text_width(rect.w, entry_data.actions.size());

	int text_height = entry_data.font.height();
	if (!entry_data.detail_lines.empty())
		text_height += TITLE_DETAIL_GAP + (static_cast<int>(entry_data.detail_lines.size()) * entry_data.line_height);

	gfx::Rect inner = get_inner_rect(rect);
	gfx::Point text_pos(thumbnail_rect.x2() + THUMBNAIL_GAP, inner.y + ((inner.h - text_height) / 2));

	std::string title = entry_data.title;
	render::clip_string(title, entry_data.font, text_width);

	render::text(text_pos, gfx::Color::white().adjust_alpha(content_anim), title, entry_data.font);

	text_pos.y += entry_data.font.height() + TITLE_DETAIL_GAP;

	gfx::Color detail_color = entry_data.error ? ERROR_DETAIL_COLOR : DETAIL_COLOR;

	for (const auto& line : entry_data.detail_lines) {
		render::text(text_pos, detail_color.adjust_alpha(content_anim), line, entry_data.font);
		text_pos.y += entry_data.line_height;
	}

	for (const auto [i, action] : u::enumerate(entry_data.actions)) {
		float action_hover_anim = element.animations.at(action_animation_key(i)).current;

		gfx::Color action_color = gfx::Color::white(static_cast<uint8_t>(u::lerp(130.f, 255.f, action_hover_anim)))
		                              .adjust_alpha(content_anim);

		render::text(
			get_action_rect(rect, i, entry_data.actions.size()).center(),
			action_color,
			action.icon,
			fonts::icons,
			FONT_CENTERED_X | FONT_CENTERED_Y
		);
	}

	render::pop_clip_rect();
}

bool ui::update_render_history_entry(const Container& container, AnimatedElement& element) {
	const auto& entry_data = std::get<RenderHistoryEntryElementData>(element.element->data);

	bool over_action = false;

	for (const auto [i, action] : u::enumerate(entry_data.actions)) {
		gfx::Rect action_rect = get_action_rect(element.element->rect, i, entry_data.actions.size());

		bool action_hovered = action_rect.contains(keys::mouse_pos) && set_hovered_element(element);
		over_action |= action_hovered;

		element.animations.at(action_animation_key(i)).set_goal(action_hovered ? 1.f : 0.f);

		if (!action_hovered)
			continue;

		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

		if (!action.tooltip.empty())
			tooltip::set(action.tooltip);

		if (keys::is_mouse_down()) {
			action.on_press();
			keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

			return true;
		}
	}

	bool hovered = over_action || (element.element->rect.contains(keys::mouse_pos) && set_hovered_element(element));
	element.animations.at(hasher("hover")).set_goal(hovered ? 1.f : 0.f);

	if (hovered && !over_action && entry_data.on_click) {
		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

		if (keys::is_mouse_down()) {
			(*entry_data.on_click)();
			keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

			return true;
		}
	}

	return false;
}

ui::AnimatedElement* ui::add_render_history_entry(
	const std::string& id,
	Container& container,
	const std::string& title,
	const std::string& detail,
	bool error,
	const std::shared_ptr<render::Texture>& thumbnail,
	const std::vector<RenderHistoryAction>& actions,
	std::optional<std::function<void()>> on_click,
	const std::optional<gfx::Rect>& collapse_rect,
	const render::Font& font
) {
	gfx::Size size(container.get_usable_rect().w, 0);

	const int line_height = font.height() + 3;
	const int text_width = get_text_width(size.w, actions.size());

	std::vector<std::string> detail_lines;
	if (!detail.empty()) {
		if (error) {
			// errors get the room to wrap, anything else is a filename and just gets clipped
			detail_lines = render::wrap_text(detail, gfx::Size(text_width, 0), font, line_height);

			if (detail_lines.size() > MAX_DETAIL_LINES) {
				detail_lines.resize(MAX_DETAIL_LINES);
				detail_lines.back() += "...";
				render::clip_string(detail_lines.back(), font, text_width);
			}
		}
		else {
			detail_lines = { detail };
			render::clip_string(detail_lines.back(), font, text_width);
		}
	}

	int text_height = font.height();
	if (!detail_lines.empty())
		text_height += TITLE_DETAIL_GAP + (static_cast<int>(detail_lines.size()) * line_height);

	size.h = std::max(THUMBNAIL_H, text_height) + (ENTRY_PADDING * 2);

	Element element(
		id,
		ElementType::RENDER_HISTORY_ENTRY,
		gfx::Rect(container.current_position, size),
		RenderHistoryEntryElementData{
			.title = title,
			.detail_lines = detail_lines,
			.error = error,
			.thumbnail = thumbnail,
			.actions = actions,
			.on_click = std::move(on_click),
			.collapse_rect = collapse_rect,
			.font = font,
			.line_height = line_height,
		},
		render_render_history_entry,
		update_render_history_entry
	);

	std::unordered_map<size_t, AnimationState> animations = {
		{ hasher("main"), AnimationState(15.f) },
		{ hasher("hover"), AnimationState(80.f) },
	};

	for (size_t i = 0; i < actions.size(); i++) {
		animations.emplace(action_animation_key(i), AnimationState(80.f));
	}

	return add_element(container, std::move(element), container.element_gap, animations);
}
