#pragma once

class VideoPlayer;

namespace render {
	class Texture;
}

namespace ui {
	// shown in place of a video's own frame, e.g. a preview of its output
	struct Frame {
		std::shared_ptr<VideoPlayer> player;

		// shown instead of the player when it's set. the id changes with the texture
		std::shared_ptr<render::Texture> texture;
		size_t texture_id = 0;

		// dimmed like the video, while it stands in for what's on its way
		bool faded = false;

		bool operator==(const Frame& other) const = default;
	};
}
