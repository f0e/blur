#include "gui_utils.h"
#include "render/render.h"

namespace {
	enum class ThumbnailState : std::uint8_t {
		GENERATING,
		PENDING_UPLOAD,
		READY,
		FAILED
	};

	struct Thumbnail {
		ThumbnailState state = ThumbnailState::GENERATING;
		SDL_Surface* surface = nullptr;
		std::shared_ptr<render::Texture> texture;
	};

	std::unordered_map<std::string, Thumbnail> thumbnails;
	std::mutex thumbnail_mutex;

	SDL_Surface* ffmpeg_get_thumbnail_surface(
		const std::filesystem::path& path, std::optional<gfx::Size> size, double timestamp
	) {
		namespace bp = boost::process;

		bp::ipstream pipe_stream;

		std::vector<std::string> ffmpeg_args = {
			"-v", "error", "-ss", std::to_string(timestamp), "-i", u::path_to_string(path), "-frames:v", "1",
		};

		if (size) {
			ffmpeg_args.insert(
				ffmpeg_args.end(),
				{
					"-vf",
					"scale=" + std::to_string(size->w) + ":" + std::to_string(size->h),
				}
			);
		}

		ffmpeg_args.insert(
			ffmpeg_args.end(),
			{
				"-f",
				"image2pipe",
				"-vcodec",
				"mjpeg",
				"-",
			}
		);

		auto c = u::run_command(blur.ffmpeg_path, ffmpeg_args, bp::std_out > pipe_stream, bp::std_err > stdout);

		std::vector<char> buffer{ std::istreambuf_iterator<char>(pipe_stream), std::istreambuf_iterator<char>() };
		c.wait();

		if (buffer.empty())
			return nullptr;

		return render::jpeg_bytes_to_surface(buffer.data(), buffer.size());
	}

	void ffmpeg_get_thumbnail_surface_async(
		const std::filesystem::path& video_path, const std::string& key, std::optional<gfx::Size> size, double timestamp
	) {
		u::log("spawning thumbnail thread for {}", u::path_to_string(video_path));

		std::thread([video_path, key, size, timestamp] {
			SDL_Surface* surface = ffmpeg_get_thumbnail_surface(video_path, size, timestamp);

			std::unique_lock lock(thumbnail_mutex);

			auto& thumb = thumbnails[key];
			if (!surface) {
				thumb.state = ThumbnailState::FAILED;
				return;
			}

			thumb.surface = surface;
			thumb.state = ThumbnailState::PENDING_UPLOAD;
		}).detach();
	}
}

std::optional<gui_utils::ThumbnailRes> gui_utils::get_thumbnail(
	const std::filesystem::path& video_path, std::optional<gfx::Size> size, double timestamp
) {
	const auto key = video_path.string();

	std::unique_lock lock(thumbnail_mutex);

	auto it = thumbnails.find(key);

	if (it == thumbnails.end()) {
		thumbnails[key] = {};

		ffmpeg_get_thumbnail_surface_async(video_path, key, size, timestamp);

		return {};
	}

	auto& thumb = it->second;

	switch (thumb.state) {
		case ThumbnailState::FAILED:
			return ThumbnailRes{ .error = "failed to generate thumbnail" };

		case ThumbnailState::PENDING_UPLOAD: {
			// need to turn surface into opengl texture
			// (has to run on render thread)
			auto tex = std::make_shared<render::Texture>();
			tex->load_from_surface(thumb.surface);

			SDL_DestroySurface(thumb.surface);
			thumb.surface = nullptr;

			if (!tex->is_valid()) {
				thumb.state = ThumbnailState::FAILED;
				return ThumbnailRes{ .error = "failed to generate thumbnail" };
			}

			thumb.texture = tex;
			thumb.state = ThumbnailState::READY;

			return ThumbnailRes{ .texture = thumb.texture };
		}

		case ThumbnailState::READY:
			return ThumbnailRes{ .texture = thumb.texture };

		default:
		case ThumbnailState::GENERATING:
			return {};
	}

	return {};
}

void gui_utils::delete_thumbnail(const std::filesystem::path& video_path) {
	std::unique_lock lock(thumbnail_mutex);

	auto it = thumbnails.find(video_path.string());
	if (it == thumbnails.end())
		return;

	if (it->second.surface)
		SDL_DestroySurface(it->second.surface);

	thumbnails.erase(it);
}
