#include "../ui.h"
#include "../../render/render.h"
#include "../keys.h"

static const int ENTRY_PADDING = ui::RENDER_HISTORY_ENTRY_PADDING;
static const float ENTRY_ROUNDING = 6.f;

static const int THUMBNAIL_W = 71; // 16:9, matching the height below
static const int THUMBNAIL_H = 40;
static const int THUMBNAIL_GAP = 9;
static const float THUMBNAIL_ROUNDING = 4.f;

static const int ACTION_SIZE = ui::RENDER_HISTORY_ACTION_SIZE;
static const int ACTION_GAP = 2;
static const int ACTION_LABEL_PADDING = 7;

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

	int get_action_width(const ui::RenderHistoryAction& action, const render::Font& font) {
		if (action.label.empty())
			return ACTION_SIZE;

		return font.calc_size(action.label).w + (ACTION_LABEL_PADDING * 2);
	}

	int get_actions_width(const std::vector<ui::RenderHistoryAction>& actions, const render::Font& font) {
		if (actions.empty())
			return 0;

		int width = (static_cast<int>(actions.size()) - 1) * ACTION_GAP;
		for (const auto& action : actions)
			width += get_action_width(action, font);

		return width;
	}

	// action buttons sit in a row against the right edge, centered vertically
	std::vector<gfx::Rect> get_action_rects(
		const gfx::Rect& entry_rect, const std::vector<ui::RenderHistoryAction>& actions, const render::Font& font
	) {
		gfx::Rect inner = get_inner_rect(entry_rect);
		int x = inner.x2() - get_actions_width(actions, font);

		std::vector<gfx::Rect> rects;
		rects.reserve(actions.size());

		for (const auto& action : actions) {
			int width = get_action_width(action, font);

			rects.emplace_back(x, inner.y + ((inner.h - ACTION_SIZE) / 2), width, ACTION_SIZE);
			x += width + ACTION_GAP;
		}

		return rects;
	}

	// the space the title and detail lines get, between the thumbnail and the actions
	int get_text_width(int entry_width, const std::vector<ui::RenderHistoryAction>& actions, const render::Font& font) {
		int width = entry_width - (ENTRY_PADDING * 2) - THUMBNAIL_W - THUMBNAIL_GAP;

		if (!actions.empty())
			width -= get_actions_width(actions, font) + THUMBNAIL_GAP;

		return std::max(width, 0);
	}

	// the title's line, with the detail lines stacked under it
	int get_text_height(const std::vector<std::string>& detail_lines, const render::Font& font, int line_height) {
		int height = font.height();

		if (!detail_lines.empty())
			height += TITLE_DETAIL_GAP + (static_cast<int>(detail_lines.size()) * line_height);

		return height;
	}

	size_t action_animation_key(size_t index) {
		return ui::hasher(std::format("action_hover_{}", index));
	}

	// on the way in the row grows out of the history button, on the way out it shrinks back into it
	gfx::Rect get_animated_rect(const ui::RenderHistoryEntryElementData& data, const gfx::Rect& rect, float anim) {
		if (!data.collapse_rect || anim >= 1.f)
			return rect;

		return gfx::Rect::lerp(*data.collapse_rect, rect, anim);
	}
}

void ui::render_render_history_entry(const Container& container, const AnimatedElement& element) {
	const auto& entry_data = std::get<RenderHistoryEntryElementData>(element.element->data);

	float anim = element.animations.at(hasher("main")).current;
	float hover_anim = element.animations.at(hasher("hover")).current;

	if (anim <= 0.01f)
		return;

	const gfx::Rect& rect = element.element->rect;
	gfx::Rect animated_rect = get_animated_rect(entry_data, rect, anim);
	size_t first_vertex = render::draw_vertex_count();

	gfx::Color fill_color = entry_data.error
	                            ? gfx::Color::lerp(gfx::Color(38, 20, 20), gfx::Color(48, 26, 26), hover_anim)
	                            : gfx::Color::lerp(gfx::Color(20, 20, 20), gfx::Color(28, 28, 28), hover_anim);
	gfx::Color stroke_color = entry_data.error ? gfx::Color(120, 60, 60, 110) : gfx::Color::white(45);

	render::rounded_rect_filled(rect, fill_color, ENTRY_ROUNDING);
	render::rounded_rect_stroke(rect, stroke_color, ENTRY_ROUNDING);

	// thumbnail, or a placeholder while one's still being generated
	gfx::Rect thumbnail_rect = get_thumbnail_rect(rect);

	render::rounded_rect_filled(thumbnail_rect, gfx::Color::white(10), THUMBNAIL_ROUNDING);

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

		render::image_rounded(image_rect, texture, THUMBNAIL_ROUNDING);
	}

	render::rounded_rect_stroke(thumbnail_rect, gfx::Color::white(45), THUMBNAIL_ROUNDING);

	// title above the detail lines, the block centered against the thumbnail
	int text_height = get_text_height(entry_data.detail_lines, entry_data.font, entry_data.line_height);

	gfx::Rect inner = get_inner_rect(rect);
	gfx::Point text_pos(thumbnail_rect.x2() + THUMBNAIL_GAP, inner.y + ((inner.h - text_height) / 2));

	// the title and the detail lines were clipped to fit when the row was added
	render::text(text_pos, gfx::Color::white(), entry_data.title, entry_data.font);

	text_pos.y += entry_data.font.height() + TITLE_DETAIL_GAP;

	gfx::Color detail_color = entry_data.error ? ERROR_DETAIL_COLOR : DETAIL_COLOR;

	for (const auto& line : entry_data.detail_lines) {
		render::text(text_pos, detail_color, line, entry_data.font);
		text_pos.y += entry_data.line_height;
	}

	std::vector<gfx::Rect> action_rects = get_action_rects(rect, entry_data.actions, entry_data.font);

	for (const auto [i, action] : u::enumerate(entry_data.actions)) {
		float action_hover_anim = element.animations.at(action_animation_key(i)).current;
		const gfx::Rect& action_rect = action_rects[i];

		gfx::Color action_color = gfx::Color::white(static_cast<uint8_t>(u::lerp(130.f, 255.f, action_hover_anim)));

		if (!action.label.empty()) {
			render::rounded_rect_filled(
				action_rect, gfx::Color::white(static_cast<uint8_t>(u::lerp(8.f, 22.f, action_hover_anim))), 4.f
			);
			render::rounded_rect_stroke(
				action_rect, gfx::Color::white(static_cast<uint8_t>(u::lerp(38.f, 70.f, action_hover_anim))), 4.f
			);
			render::text(
				action_rect.center(), action_color, action.label, entry_data.font, FONT_CENTERED_X | FONT_CENTERED_Y
			);
		}
		else {
			render::text(
				action_rect.center(), action_color, action.icon, fonts::icons, FONT_CENTERED_X | FONT_CENTERED_Y
			);
		}
	}

	// Scale everything submitted for the row together, including its text and thumbnail.
	render::transform_draw_vertices(first_vertex, rect, animated_rect, anim);
}

bool ui::update_render_history_entry(const Container& container, AnimatedElement& element) {
	const auto& entry_data = std::get<RenderHistoryEntryElementData>(element.element->data);

	// hit test where the row is drawn, not where it was laid out - while it's still folding out of the button it
	// covers a fraction of its final bounds, and clicking empty space shouldn't hit a row that isn't there yet
	gfx::Rect rect =
		get_animated_rect(entry_data, element.element->rect, element.animations.at(hasher("main")).current);

	bool over_action = false;

	std::vector<gfx::Rect> action_rects = get_action_rects(rect, entry_data.actions, entry_data.font);

	for (const auto [i, action] : u::enumerate(entry_data.actions)) {
		bool action_hovered = action_rects[i].contains(keys::mouse_pos) && set_hovered_element(element);
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

	bool hovered = over_action || (rect.contains(keys::mouse_pos) && set_hovered_element(element));
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
	const int text_width = get_text_width(size.w, actions, font);

	// errors get the room to wrap, anything else is a filename and just gets clipped
	std::vector<std::string> detail_lines;
	if (!detail.empty()) {
		detail_lines = error ? render::wrap_text(detail, gfx::Size(text_width, 0), font, line_height)
		                     : std::vector<std::string>{ detail };

		if (detail_lines.size() > MAX_DETAIL_LINES) {
			detail_lines.resize(MAX_DETAIL_LINES);
			detail_lines.back() += "...";
		}

		render::clip_string(detail_lines.back(), font, text_width);
	}

	std::string clipped_title = title;
	render::clip_string(clipped_title, font, text_width);

	size.h = std::max(THUMBNAIL_H, get_text_height(detail_lines, font, line_height)) + (ENTRY_PADDING * 2);

	Element element(
		id,
		ElementType::RENDER_HISTORY_ENTRY,
		gfx::Rect(container.current_position, size),
		RenderHistoryEntryElementData{
			.title = std::move(clipped_title),
			.detail_lines = std::move(detail_lines),
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
