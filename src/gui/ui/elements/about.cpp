#include "../ui.h"
#include "../../render/render.h"
#include "../keys.h"

static const int LOGO_SIZE = 52;
static const int LOGO_GAP = 12;
static const int SUBTITLE_GAP = 2;
static const int LINKS_GAP = 14;
static const int LINK_GAP = 16;
static const int LINK_HIT_PAD = 4;

namespace {
	size_t link_animation_key(size_t index) {
		return ui::hasher(std::format("about link {} hover", index));
	}

	int text_block_height(const ui::AboutElementData& data) {
		int height = data.title_font->height();
		if (!data.subtitle.empty())
			height += SUBTITLE_GAP + data.font->height();
		return height;
	}

	int text_block_width(const ui::AboutElementData& data) {
		return std::max(data.title_font->calc_size(data.title).w, data.font->calc_size(data.subtitle).w);
	}

	int header_height(const ui::AboutElementData& data) {
		return std::max(LOGO_SIZE, text_block_height(data));
	}

	int links_width(const ui::AboutElementData& data) {
		int width = 0;
		for (const auto& link : data.links) {
			if (width > 0)
				width += LINK_GAP;
			width += data.font->calc_size(link.text).w;
		}
		return width;
	}

	// the row of links, centered under the logo and title
	std::vector<gfx::Rect> get_link_rects(const gfx::Rect& rect, const ui::AboutElementData& data) {
		std::vector<gfx::Rect> rects(data.links.size());

		int height = data.font->height();
		int y = rect.y2() - height;
		int x = rect.center().x - (links_width(data) / 2);

		for (const auto [i, link] : u::enumerate(data.links)) {
			int width = data.font->calc_size(link.text).w;
			rects[i] = gfx::Rect(x, y, width, height);
			x += width + LINK_GAP;
		}

		return rects;
	}
}

void ui::render_about(const Container& container, const AnimatedElement& element) {
	const auto& about_data = std::get<AboutElementData>(element.element->data);
	float anim = element.animations.at(hasher("main")).current;

	const auto& rect = element.element->rect;

	int header_h = header_height(about_data);
	int header_w = LOGO_SIZE + LOGO_GAP + text_block_width(about_data);
	int header_x = rect.center().x - (header_w / 2);

	gfx::Rect logo_rect(header_x, rect.y + ((header_h - LOGO_SIZE) / 2), LOGO_SIZE, LOGO_SIZE);
	if (about_data.logo)
		render::image(logo_rect, *about_data.logo, gfx::Color::white().adjust_alpha(anim));

	gfx::Point text_pos(logo_rect.x2() + LOGO_GAP, rect.y + ((header_h - text_block_height(about_data)) / 2));

	render::text(
		text_pos, gfx::Color::white(static_cast<uint8_t>(anim * 255.f)), about_data.title, *about_data.title_font
	);

	if (!about_data.subtitle.empty()) {
		text_pos.y += about_data.title_font->height() + SUBTITLE_GAP;

		render::text(
			text_pos, gfx::Color::white(static_cast<uint8_t>(anim * 105.f)), about_data.subtitle, *about_data.font
		);
	}

	auto link_rects = get_link_rects(rect, about_data);

	for (const auto [i, link] : u::enumerate(about_data.links)) {
		float hover = element.animations.at(link_animation_key(i)).current;

		gfx::Color color = gfx::Color::white(static_cast<uint8_t>(anim * (105.f + (150.f * hover))));

		render::text(link_rects[i].top_left(), color, link.text, *about_data.font);

		// underline fades in on hover so it reads as clickable
		if (hover > 0.f) {
			gfx::Rect underline(link_rects[i].x, link_rects[i].y2() + 1, link_rects[i].w, 1);
			render::rect_filled(underline, color.adjust_alpha(hover * 0.6f));
		}
	}
}

bool ui::update_about(const Container& container, AnimatedElement& element) {
	const auto& about_data = std::get<AboutElementData>(element.element->data);

	auto link_rects = get_link_rects(element.element->rect, about_data);

	bool updated = false;

	for (const auto [i, link] : u::enumerate(about_data.links)) {
		auto& anim = element.animations.at(link_animation_key(i));

		bool hovered = link_rects[i].expand(LINK_HIT_PAD).contains(keys::mouse_pos) && set_hovered_element(element);
		anim.set_goal(hovered ? 1.f : 0.f);

		if (!hovered || !link.on_press)
			continue;

		set_cursor(SDL_SYSTEM_CURSOR_POINTER);

		if (!keys::is_mouse_down())
			continue;

		(*link.on_press)();
		keys::on_mouse_press_handled(SDL_BUTTON_LEFT);

		updated = true;
	}

	return updated;
}

ui::AnimatedElement* ui::add_about(
	const std::string& id,
	Container& container,
	std::shared_ptr<render::Texture> logo,
	const std::string& title,
	const std::string& subtitle,
	const std::vector<AboutLink>& links,
	const render::Font& title_font,
	const render::Font& font
) {
	AboutElementData data{
		.logo = std::move(logo),
		.title = title,
		.subtitle = subtitle,
		.links = links,
		.title_font = &title_font,
		.font = &font,
	};

	gfx::Size size(std::max(LOGO_SIZE + LOGO_GAP + text_block_width(data), links_width(data)), header_height(data));

	if (!links.empty())
		size.h += LINKS_GAP + font.height();

	Element element(
		id, ElementType::ABOUT, gfx::Rect(container.current_position, size), data, render_about, update_about
	);

	std::unordered_map<size_t, AnimationState> animations{
		{ hasher("main"), AnimationState(15.f) },
	};
	for (size_t i = 0; i < links.size(); i++) {
		animations.emplace(link_animation_key(i), AnimationState(80.f));
	}

	return add_element(container, std::move(element), container.element_gap, animations);
}
