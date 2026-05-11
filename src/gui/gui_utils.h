#pragma once

namespace render {
	class Texture;
}

namespace gui_utils {
	struct ThumbnailRes {
		std::string error;
		std::shared_ptr<render::Texture> texture;

		bool operator==(const ThumbnailRes& other) const = default;
	};

	// must be called from main thread
	std::optional<ThumbnailRes> get_thumbnail(
		const std::filesystem::path& video_path, std::optional<gfx::Size> size = {}, double timestamp = 0.0
	);

	void delete_thumbnail(const std::filesystem::path& video_path);
}
