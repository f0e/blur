#include "../ui.h"
#include "../../render/render.h"

static const int LOGO_SIZE = 52;
static const int LOGO_GAP = 12;
static const int SUBTITLE_GAP = 2;

namespace {
	int text_block_height(const ui::LogoAndVersionElementData& data) {
		int height = data.title_font->height();
		if (!data.subtitle.empty())
			height += SUBTITLE_GAP + data.font->height();
		return height;
	}

	int text_block_width(const ui::LogoAndVersionElementData& data) {
		return std::max(data.title_font->calc_size(data.title).w, data.font->calc_size(data.subtitle).w);
	}

	int header_height(const ui::LogoAndVersionElementData& data) {
		return std::max(LOGO_SIZE, text_block_height(data));
	}
}

void ui::render_logo_and_version(const Container& container, const AnimatedElement& element) {
	const auto& data = std::get<LogoAndVersionElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	const auto& rect = element.element->rect;

	int header_h = header_height(data);

	gfx::Rect logo_rect(rect.x, rect.y + ((header_h - LOGO_SIZE) / 2), LOGO_SIZE, LOGO_SIZE);
	if (data.logo)
		render::image(logo_rect, *data.logo, gfx::Color::white().adjust_alpha(anim));

	gfx::Point text_pos(logo_rect.x2() + LOGO_GAP, rect.y + ((header_h - text_block_height(data)) / 2));

	render::text(text_pos, gfx::Color::white(static_cast<uint8_t>(anim * 255.f)), data.title, *data.title_font);

	if (!data.subtitle.empty()) {
		text_pos.y += data.title_font->height() + SUBTITLE_GAP;

		render::text(text_pos, gfx::Color::white(static_cast<uint8_t>(anim * 105.f)), data.subtitle, *data.font);
	}
}

ui::AnimatedElement* ui::add_logo_and_version(
	const std::string& id,
	Container& container,
	std::shared_ptr<render::Texture> logo,
	const std::string& title,
	const std::string& subtitle,
	const render::Font& title_font,
	const render::Font& font
) {
	LogoAndVersionElementData data{
		.logo = std::move(logo),
		.title = title,
		.subtitle = subtitle,
		.title_font = &title_font,
		.font = &font,
	};

	gfx::Size size(LOGO_SIZE + LOGO_GAP + text_block_width(data), header_height(data));

	Element element(
		id, ElementType::LOGO_AND_VERSION, gfx::Rect(container.current_position, size), data, render_logo_and_version
	);

	return add_element(
		container, std::move(element), container.element_gap, { { hasher("main"), AnimationState(15.f) } }
	);
}
