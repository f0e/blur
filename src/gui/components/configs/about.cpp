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
	ui::add_logo_and_version(
		"logo and version",
		container,
		get_logo_texture(),
		"blur",
		std::format("v{}", BLUR_VERSION),
		fonts::header_font,
		fonts::dejavu
	);

	ui::add_link("github link", container, "github", fonts::dejavu, [] {
		SDL_OpenURL(GITHUB_URL.c_str());
	});

	ui::set_next_same_line(container);

	ui::add_link("discord link", container, "discord", fonts::dejavu, [] {
		SDL_OpenURL(DISCORD_URL.c_str());
	});

	ui::add_button("open config folder", container, "Open config folder", fonts::dejavu, [] {
		std::string file_url = std::format("file://{}", blur.settings_path);
		if (!SDL_OpenURL(file_url.c_str())) {
			u::log_error("Failed to open config folder: {}", SDL_GetError());
		}
	});

	gui::components::update_notice::render(container, ui::UpdateNoticeAlign::CENTER, false);

	if (!gui::components::update_notice::is_available()) {
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
}
