#include "../ui.h"
#include "../../render/render.h"
#include "../keys.h"

static const int RULE_HEIGHT = 1;
static const int RULE_GAP = 9;
static const int SUBTEXT_GAP = 3;
static const int LINE_GAP = 5;
static const int LINK_GAP = 14;
static const int LINK_HIT_PAD = 4;

namespace {
	size_t link_animation_key(size_t row, size_t index) {
		return ui::hasher(std::format("link {} {} hover", row, index));
	}

	// each row is packed against the right edge, rows stacked bottom to top
	std::vector<std::vector<gfx::Rect>> get_link_rects(const gfx::Rect& rect, const ui::UpdateNoticeElementData& data) {
		std::vector<std::vector<gfx::Rect>> rects(data.lines.size());

		int height = data.font->height();
		int y = rect.y2() - height;

		for (size_t row = data.lines.size(); row-- > 0;) {
			const auto& line = data.lines[row];
			rects[row].resize(line.size());

			int right = rect.x2();
			for (size_t i = line.size(); i-- > 0;) {
				int width = data.font->calc_size(line[i].text).w;
				rects[row][i] = gfx::Rect(right - width, y, width, height);
				right -= width + LINK_GAP;
			}

			y -= LINE_GAP + height;
		}

		return rects;
	}

	void render_link(
		const gfx::Rect& link_rect, const ui::UpdateNoticeLink& link, const render::Font& font, float anim, float hover
	) {
		gfx::Color color = link.primary ? gfx::Color::white(static_cast<uint8_t>(anim * (150.f + (105.f * hover))))
		                                : gfx::Color::white(static_cast<uint8_t>(anim * (85.f + (95.f * hover))));

		render::text(link_rect.top_right(), color, link.text, font, FONT_RIGHT_ALIGN);

		// underline fades in on hover so it reads as clickable
		if (hover > 0.f) {
			gfx::Rect underline(link_rect.x, link_rect.y2() + 1, link_rect.w, 1);
			render::rect_filled(underline, color.adjust_alpha(hover * 0.6f));
		}
	}

	bool update_link(
		ui::AnimatedElement& element,
		const gfx::Rect& link_rect,
		const std::optional<std::function<void()>>& on_press,
		size_t animation_key
	) {
		auto& anim = element.animations.at(animation_key);

		bool hovered = link_rect.expand(LINK_HIT_PAD).contains(keys::mouse_pos) && ui::set_hovered_element(element);
		anim.set_goal(hovered ? 1.f : 0.f);

		if (!hovered || !on_press)
			return false;

		ui::set_cursor(SDL_SYSTEM_CURSOR_POINTER);

		if (!keys::is_mouse_down())
			return false;

		(*on_press)();
		keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

		return true;
	}
}

void ui::render_update_notice(const Container& container, const AnimatedElement& element) {
	const auto& notice_data = std::get<UpdateNoticeElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	const auto& rect = element.element->rect;

	// top separator
	gfx::Rect rule_rect(rect.x, rect.y, rect.w, RULE_HEIGHT);
	render::rect_filled_gradient(
		rule_rect,
		render::GradientDirection::GRADIENT_LEFT,
		{ gfx::Color::white(static_cast<uint8_t>(anim * 40.f)), gfx::Color::white(0) }
	);

	// top separator download progress bar
	if (notice_data.progress) {
		gfx::Rect progress_rect = rule_rect;
		progress_rect.w = static_cast<int>(std::round(rule_rect.w * std::clamp(*notice_data.progress, 0.f, 1.f)));
		progress_rect.x = rule_rect.x2() - progress_rect.w;

		render::rect_filled_gradient(
			progress_rect,
			render::GradientDirection::GRADIENT_LEFT,
			{ gfx::Color::white(static_cast<uint8_t>(anim * 160.f)),
		      gfx::Color::white(static_cast<uint8_t>(anim * 40.f)) }
		);
	}

	gfx::Point text_pos(rect.x2(), rule_rect.y2() + RULE_GAP);
	render::text(
		text_pos,
		gfx::Color::white(static_cast<uint8_t>(anim * 210.f)),
		notice_data.status,
		*notice_data.font,
		FONT_RIGHT_ALIGN
	);

	if (!notice_data.subtext.empty()) {
		text_pos.y += notice_data.font->height() + SUBTEXT_GAP;

		render::text(
			text_pos,
			gfx::Color::white(static_cast<uint8_t>(anim * 105.f)),
			notice_data.subtext,
			*notice_data.font,
			FONT_RIGHT_ALIGN
		);
	}

	auto line_rects = get_link_rects(rect, notice_data);

	for (const auto [row, line] : u::enumerate(notice_data.lines)) {
		for (const auto [i, link] : u::enumerate(line)) {
			render_link(
				line_rects[row][i],
				link,
				*notice_data.font,
				anim,
				element.animations.at(link_animation_key(row, i)).current
			);
		}
	}
}

bool ui::update_update_notice(const Container& container, AnimatedElement& element) {
	const auto& notice_data = std::get<UpdateNoticeElementData>(element.element->data);

	auto line_rects = get_link_rects(element.element->rect, notice_data);

	bool updated = false;
	for (const auto [row, line] : u::enumerate(notice_data.lines)) {
		for (const auto [i, link] : u::enumerate(line)) {
			updated |= update_link(element, line_rects[row][i], link.on_press, link_animation_key(row, i));
		}
	}

	return updated;
}

ui::AnimatedElement* ui::add_update_notice(
	const std::string& id,
	Container& container,
	const std::string& status,
	const std::string& subtext,
	const std::vector<std::vector<UpdateNoticeLink>>& lines,
	std::optional<float> progress,
	const render::Font& font
) {
	int max_line_width = 0;
	for (const auto& line : lines) {
		int line_width = 0;
		for (const auto& link : line) {
			if (line_width > 0)
				line_width += LINK_GAP;
			line_width += font.calc_size(link.text).w;
		}
		max_line_width = std::max(max_line_width, line_width);
	}

	gfx::Size size(
		std::max({ font.calc_size(status).w, font.calc_size(subtext).w, max_line_width }), RULE_HEIGHT + RULE_GAP
	);
	size.h += font.height();
	if (!subtext.empty())
		size.h += SUBTEXT_GAP + font.height();
	for (const auto& line : lines) {
		if (!line.empty())
			size.h += LINE_GAP + font.height();
	}

	gfx::Point position(container.get_usable_rect().x2() - size.w, container.current_position.y);

	Element element(
		id,
		ElementType::UPDATE_NOTICE,
		gfx::Rect(position, size),
		UpdateNoticeElementData{
			.status = status,
			.subtext = subtext,
			.lines = lines,
			.progress = progress,
			.font = &font,
		},
		render_update_notice,
		update_update_notice
	);

	std::unordered_map<size_t, AnimationState> animations{
		{ hasher("main"), AnimationState(15.f) },
	};
	for (const auto [row, line] : u::enumerate(lines)) {
		for (size_t i = 0; i < line.size(); i++) {
			animations.emplace(link_animation_key(row, i), AnimationState(80.f));
		}
	}

	return add_element(container, std::move(element), container.element_gap, animations);
}
