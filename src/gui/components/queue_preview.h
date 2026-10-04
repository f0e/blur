#pragma once

#include "../ui/ui.h"
#include "../tasks.h"

// the blurred or mask preview over the queue's video player
namespace gui::components::queue_preview {
	// set by the queue's checkboxes, at most one's on
	inline bool blur_enabled = false;
	inline bool mask_enabled = false;

	struct State {
		std::optional<ui::VideoOverlay> overlay;
		std::optional<std::string> status;
	};

	// the video's config, with its own mask choices
	BlurSettings settings(const tasks::PendingVideo& pending_video);

	State update(const tasks::PendingVideo& pending_video, const GlobalAppSettings& app_settings);

	void handle_event(const SDL_Event& event, bool& to_render);

	// shift+space starts a pre-render, space moves it on. false if the video player should have the key
	bool handle_key_press(SDL_Keycode key, SDL_Keymod mod);

	// its vapoursynth core and any gpu memory aren't worth keeping while the queue's not up
	void release();
}
