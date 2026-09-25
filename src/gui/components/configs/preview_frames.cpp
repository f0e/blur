#include "preview_frames.h"

#include "common/rendering/render.h"
#include "common/rendering/render_state.h"

#include "../notifications.h"
#include "../../ui/helpers/video.h"
#include "common/media.h"

namespace preview_frames = gui::components::configs::preview_frames;

namespace {
	// stops a held slider from starting a render every ui frame
	constexpr auto DEBOUNCE_TIME = std::chrono::milliseconds(50);

	struct FrameKey {
		float seek = 0.f;
		BlurSettings settings;
	};

	struct RenderedFrame {
		FrameKey key;
		std::vector<uint8_t> jpeg;
		std::string frame_timing_log;
	};

	struct LastRender {
		FrameKey key;
		std::chrono::steady_clock::time_point time;
	};

	struct Preview {
		bool mask = false;

		std::optional<RenderedFrame> pending; // guarded by render_mutex

		std::optional<RenderedFrame> shown;
		std::shared_ptr<render::Texture> texture;
		std::string image_id;

		std::optional<LastRender> last_render;

		[[nodiscard]] bool same_frame(const FrameKey& a, const FrameKey& b) const {
			// a mask is worked out from the whole video, so the seek doesn't matter
			if (mask)
				return config_blur::same_masking(a.settings, b.settings);

			return a.seek == b.seek && a.settings == b.settings;
		}
	};

	Preview blurred_preview{ .mask = false };
	Preview mask_preview{ .mask = true };

	// ui::add_image keeps its texture while the id matches, so an id can't be handed out twice
	size_t image_count = 0;

	struct ActiveRender {
		std::shared_ptr<rendering::RenderState> state;
		Preview* preview;
	};

	// only the newest render publishes, anything it replaced has been stopped
	std::mutex render_mutex;
	std::optional<ActiveRender> active_render;

	std::shared_ptr<VideoPlayer> source_player;
	std::optional<float> source_timestamp;

	struct {
		std::mutex mutex;
		std::filesystem::path path;
		media::VideoInfo info;
	} video;

	void fetch_video_info(const std::filesystem::path& path) {
		std::thread([path] {
			auto info = media::get_video_info(path);

			std::lock_guard lock(video.mutex);

			if (video.path == path)
				video.info = info;
		}).detach();
	}

	std::shared_ptr<rendering::RenderState> start_render(Preview& preview) {
		std::lock_guard lock(render_mutex);

		if (active_render) {
			active_render->state->stop();

			// so switching back to the other view starts its render again
			if (active_render->preview != &preview)
				active_render->preview->last_render.reset();
		}

		auto state = std::make_shared<rendering::RenderState>();
		active_render = ActiveRender{ .state = state, .preview = &preview };
		return state;
	}

	void stop_render() {
		std::lock_guard lock(render_mutex);

		if (active_render)
			active_render->state->stop();

		active_render.reset();
	}

	void request_render(const preview_frames::Request& request, Preview& preview, const FrameKey& key) {
		// positions a drag passes through would be thrown away on the next mouse move
		if (request.seeking)
			return;

		auto now = std::chrono::steady_clock::now();
		const auto& last = preview.last_render;

		if (last && (preview.same_frame(last->key, key) || now - last->time < DEBOUNCE_TIME))
			return;

		u::log(preview.mask ? "generating mask preview" : "generating config preview");

		auto state = start_render(preview);
		preview.last_render = LastRender{ .key = key, .time = now };

		std::thread([state, &preview, key, video_path = request.video_path, app_settings = request.app_settings] {
			auto res = rendering::render_frame(video_path, key.settings, app_settings, state, key.seek, preview.mask);

			std::lock_guard lock(render_mutex);

			if (!active_render || active_render->state != state)
				return;

			if (res) {
				preview.pending = RenderedFrame{
					.key = key,
					.jpeg = std::move(res->frame_jpeg),
					.frame_timing_log = state->get_progress().frame_timing_log,
				};
				u::log(preview.mask ? "mask preview finished rendering" : "config preview finished rendering");
			}
			else {
				gui::components::notifications::show_failure_notification(
					preview.mask ? "Failed to generate mask preview." : "Failed to generate config preview.",
					res.error(),
					std::chrono::duration<float>(10.f)
				);
			}

			active_render.reset();
		}).detach();
	}

	// textures can only be created on the render thread
	void upload(Preview& preview) {
		std::optional<RenderedFrame> pending;
		{
			std::lock_guard lock(render_mutex);
			pending = std::exchange(preview.pending, std::nullopt);
		}

		if (!pending)
			return;

		auto texture = render::texture_from_jpeg(pending->jpeg);
		if (!texture)
			return;

		preview.texture = std::move(texture);
		preview.shown = std::move(pending);
		preview.image_id = std::format("config preview {}", ++image_count);
	}

	struct RenderStatus {
		bool rendering = false;
		rendering::RenderState::InitStage init_stage = rendering::RenderState::InitStage::NONE;
	};

	RenderStatus render_status(const Preview& preview) {
		std::lock_guard lock(render_mutex);

		if (!active_render || active_render->preview != &preview)
			return {};

		return {
			.rendering = true,
			.init_stage = active_render->state->get_progress().init_stage,
		};
	}

	// kept on the frame the blurred preview will render for this seek, so nothing jumps when that arrives
	void update_source_player(const preview_frames::Request& request, float seek) {
		std::optional<float> timestamp;
		{
			std::lock_guard lock(video.mutex);

			if (video.info.has_video_stream)
				timestamp = rendering::get_preview_frame_timestamp(request.settings, video.info, seek);
		}

		if (!timestamp)
			return;

		if (!source_player) {
			source_player = std::make_shared<VideoPlayer>(0.f, request.app_settings.preview_hardware_decoding);
			source_player->load_file(request.video_path, timestamp);
			source_timestamp = timestamp;
			return;
		}

		source_player->set_hardware_decoding(request.app_settings.preview_hardware_decoding);

		if (timestamp != source_timestamp) {
			source_timestamp = timestamp;
			source_player->seek(*timestamp, true);
		}
	}
}

preview_frames::Result preview_frames::update(const Request& request) {
	bool video_changed = false;
	{
		std::lock_guard lock(video.mutex);
		video_changed = video.path != request.video_path;
	}

	if (video_changed) {
		reset();

		{
			std::lock_guard lock(video.mutex);
			video.path = request.video_path;
		}

		if (!request.video_path.empty())
			fetch_video_info(request.video_path);
	}

	if (request.video_path.empty())
		return {};

	float seek = request.app_settings.config_preview_seek;
	Preview& preview = request.show_mask ? mask_preview : blurred_preview;
	FrameKey key{ .seek = request.show_mask ? 0.f : seek, .settings = request.settings };

	upload(blurred_preview);
	upload(mask_preview);

	request_render(request, preview, key);
	auto status = render_status(preview);

	Result result{
		.rendering = status.rendering,
		.init_stage = status.init_stage,
	};

	{
		std::lock_guard lock(video.mutex);
		result.video_duration = video.info.duration;
	}

	if (!request.show_mask) {
		update_source_player(request, seek);

		bool at_seek = preview.shown && preview.shown->key.seek == seek;
		bool source_drawable = source_player && source_player->has_frame() && source_player->get_video_dimensions();

		if (!at_seek && source_drawable) {
			result.frame = Frame{ .player = source_player };
			return result;
		}
	}

	if (preview.texture) {
		result.frame = Frame{
			.texture = preview.texture,
			.image_id = preview.image_id,
			.up_to_date = preview.same_frame(preview.shown->key, key) && !status.rendering,
		};

		result.frame_timing_log = preview.shown->frame_timing_log;
	}

	return result;
}

void preview_frames::handle_event(const SDL_Event& event, bool& to_render) {
	if (source_player)
		source_player->handle_mpv_event(event, to_render, true);
}

std::vector<uint8_t> preview_frames::current_mask_jpeg() {
	return mask_preview.shown ? mask_preview.shown->jpeg : std::vector<uint8_t>{};
}

void preview_frames::reset() {
	// nothing can publish once this is done, so the previews are safe to clear without the lock
	stop_render();

	blurred_preview = Preview{ .mask = false };
	mask_preview = Preview{ .mask = true };

	source_player.reset();
	source_timestamp.reset();

	std::lock_guard lock(video.mutex);
	video.path.clear();
	video.info = {};
}
