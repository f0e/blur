#include "../ui.h"
#include "../../render/render.h"

static const int RULE_HEIGHT = 1;
static const int RULE_GAP = 9;
static const int SUBTEXT_GAP = 3;
static const int LINE_GAP = 5;
static const int LINK_GAP = 14;

namespace {
	const gfx::Color PRIMARY_LINK_COLOR = gfx::Color::white(150);
	const gfx::Color PRIMARY_LINK_HOVER_COLOR = gfx::Color::white();
	const gfx::Color SECONDARY_LINK_COLOR = gfx::Color::white(85);
	const gfx::Color SECONDARY_LINK_HOVER_COLOR = gfx::Color::white(180);

	// two halves, for gradients that mirror around the middle
	std::pair<gfx::Rect, gfx::Rect> split_at_center(const gfx::Rect& rect) {
		gfx::Rect left(rect.x, rect.y, rect.w / 2, rect.h);
		gfx::Rect right(left.x2(), rect.y, rect.w - left.w, rect.h);
		return { left, right };
	}

	int line_width(const std::vector<ui::UpdateNoticeLink>& line, const render::Font& font) {
		int width = 0;
		for (const auto& link : line) {
			if (width > 0)
				width += LINK_GAP;
			width += font.calc_size(link.text).w;
		}
		return width;
	}

	// rows stacked bottom to top, each packed against the right edge or centered
	std::vector<std::vector<gfx::Rect>> get_link_rects(
		const gfx::Rect& rect,
		const std::vector<std::vector<ui::UpdateNoticeLink>>& lines,
		ui::UpdateNoticeAlign align,
		const render::Font& font
	) {
		std::vector<std::vector<gfx::Rect>> rects(lines.size());

		int height = font.height();
		int y = rect.y2() - height;

		for (size_t row = lines.size(); row-- > 0;) {
			const auto& line = lines[row];
			rects[row].resize(line.size());

			int width = line_width(line, font);
			int x = align == ui::UpdateNoticeAlign::CENTER ? rect.center().x - (width / 2) : rect.x2() - width;

			for (const auto [i, link] : u::enumerate(line)) {
				int link_width = font.calc_size(link.text).w;
				rects[row][i] = gfx::Rect(x, y, link_width, height);
				x += link_width + LINK_GAP;
			}

			y -= LINE_GAP + height;
		}

		return rects;
	}
}

void ui::render_update_notice(const Container& container, const AnimatedElement& element) {
	const auto& notice_data = std::get<UpdateNoticeElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	const auto& rect = element.element->rect;

	bool centered = notice_data.align == UpdateNoticeAlign::CENTER;

	gfx::Color rule_color = gfx::Color::white(static_cast<uint8_t>(anim * 40.f));

	// top separator, brightest where the content is anchored and fading away from it
	gfx::Rect rule_rect(rect.x, rect.y, rect.w, RULE_HEIGHT);
	if (centered) {
		auto [left, right] = split_at_center(rule_rect);

		render::rect_filled_gradient(
			left, render::GradientDirection::GRADIENT_LEFT, { rule_color, gfx::Color::white(0) }
		);
		render::rect_filled_gradient(
			right, render::GradientDirection::GRADIENT_RIGHT, { rule_color, gfx::Color::white(0) }
		);
	}
	else {
		render::rect_filled_gradient(
			rule_rect, render::GradientDirection::GRADIENT_LEFT, { rule_color, gfx::Color::white(0) }
		);
	}

	// top separator download progress bar
	if (notice_data.progress) {
		gfx::Color progress_color = gfx::Color::white(static_cast<uint8_t>(anim * 160.f));

		gfx::Rect progress_rect = rule_rect;
		progress_rect.w = static_cast<int>(std::round(rule_rect.w * std::clamp(*notice_data.progress, 0.f, 1.f)));

		if (centered) {
			// grows out from the middle, matching the fade
			progress_rect.x = rule_rect.center().x - (progress_rect.w / 2);

			auto [left, right] = split_at_center(progress_rect);

			render::rect_filled_gradient(
				left, render::GradientDirection::GRADIENT_LEFT, { progress_color, rule_color }
			);
			render::rect_filled_gradient(
				right, render::GradientDirection::GRADIENT_RIGHT, { progress_color, rule_color }
			);
		}
		else {
			progress_rect.x = rule_rect.x2() - progress_rect.w;

			render::rect_filled_gradient(
				progress_rect, render::GradientDirection::GRADIENT_LEFT, { progress_color, rule_color }
			);
		}
	}

	unsigned int text_flags = centered ? FONT_CENTERED_X : FONT_RIGHT_ALIGN;

	gfx::Point text_pos(centered ? rect.center().x : rect.x2(), rule_rect.y2() + RULE_GAP);
	render::text(
		text_pos,
		gfx::Color::white(static_cast<uint8_t>(anim * 210.f)),
		notice_data.status,
		*notice_data.font,
		text_flags
	);

	if (!notice_data.subtext.empty()) {
		text_pos.y += notice_data.font->height() + SUBTEXT_GAP;

		render::text(
			text_pos,
			gfx::Color::white(static_cast<uint8_t>(anim * 105.f)),
			notice_data.subtext,
			*notice_data.font,
			text_flags
		);
	}
}

ui::AnimatedElement* ui::add_update_notice(
	const std::string& id,
	Container& container,
	const std::string& status,
	const std::string& subtext,
	const std::vector<std::vector<UpdateNoticeLink>>& lines,
	std::optional<float> progress,
	const render::Font& font,
	UpdateNoticeAlign align
) {
	int max_line_width = 0;
	for (const auto& line : lines) {
		max_line_width = std::max(max_line_width, line_width(line, font));
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

	// centered notices get placed by the container, right aligned ones pin themselves to its right edge
	gfx::Point position(
		align == UpdateNoticeAlign::CENTER ? container.current_position.x : container.get_usable_rect().x2() - size.w,
		container.current_position.y
	);

	Element element(
		id,
		ElementType::UPDATE_NOTICE,
		gfx::Rect(position, size),
		UpdateNoticeElementData{
			.status = status,
			.subtext = subtext,
			.progress = progress,
			.align = align,
			.font = &font,
		},
		render_update_notice
	);

	auto* animated_element =
		add_element(container, std::move(element), container.element_gap, { { hasher("main"), AnimationState(15.f) } });

	auto line_rects = get_link_rects(animated_element->element->rect, lines, align, font);

	for (const auto [row, line] : u::enumerate(lines)) {
		for (const auto [i, link] : u::enumerate(line)) {
			gfx::Color color = link.primary ? PRIMARY_LINK_COLOR : SECONDARY_LINK_COLOR;
			gfx::Color hover_color = link.primary ? PRIMARY_LINK_HOVER_COLOR : SECONDARY_LINK_HOVER_COLOR;

			Element link_element(
				std::format("{} link {} {}", id, row, i),
				ElementType::LINK,
				line_rects[row][i],
				LinkElementData{
					.text = link.text,
					.on_press = link.on_press,
					.color = color,
					.hover_color = hover_color,
					.font = &font,
				},
				render_link,
				update_link
			);

			add_element(
				container,
				std::move(link_element),
				{
					{ hasher("main"), AnimationState(15.f) },
					{ hasher("hover"), AnimationState(80.f) },
				}
			);
		}
	}

	return animated_element;
}
