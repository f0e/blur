#include "configs.h"

#include "../../ui/ui.h"
#include "../../render/render.h"
#include "../../images/blur_logo.h"
#include "../update_notice.h"

#include "common/blur.h"

namespace configs = gui::components::configs;

namespace {
	const std::string GITHUB_URL = "https://github.com/f0e/blur";
	const std::string DISCORD_URL = "https://discord.gg/BQePwXqfrD";

	std::shared_ptr<render::Texture> get_logo_texture() {
		static std::shared_ptr<render::Texture> texture;
		static bool loaded = false; // only try once, it's embedded so it won't start working later

		if (!loaded) {
			loaded = true;

			SDL_Surface* surface = render::png_bytes_to_surface(BLUR_LOGO_PNG_DATA.data(), BLUR_LOGO_PNG_DATA.size());
			if (surface) {
				auto logo = std::make_shared<render::Texture>();

				if (logo->load_from_surface(surface))
					texture = logo;

				SDL_DestroySurface(surface);
			}

			if (!texture)
				u::log_error("failed to load blur logo");
		}

		return texture;
	}
}

void configs::about(ui::Container& container) {
	ui::add_about(
		"about blur",
		container,
		get_logo_texture(),
		"blur",
		std::format("v{}", BLUR_VERSION),
		{
			{
				.text = "github",
				.on_press =
					[] {
						SDL_OpenURL(GITHUB_URL.c_str());
					},
			},
			{
				.text = "discord",
				.on_press =
					[] {
						SDL_OpenURL(DISCORD_URL.c_str());
					},
			},
		},
		fonts::header_font,
		fonts::dejavu
	);

	gui::components::update_notice::render(container, ui::UpdateNoticeAlign::CENTER, false);

	if (gui::components::update_notice::is_available())
		return;

	bool checking = gui::components::update_notice::is_checking();

	std::optional<std::function<void()>> on_press;
	if (!checking) {
		on_press = [] {
			gui::components::update_notice::check_now();
		};
	}

	ui::add_button(
		"check for updates button",
		container,
		checking ? "Checking for updates..." : "Check for updates",
		fonts::dejavu,
		on_press
	);
}
