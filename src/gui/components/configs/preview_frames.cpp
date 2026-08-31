#include "preview_frames.h"

#include "common/rendering/render.h"
#include "common/rendering/render_state.h"

#include "../notifications.h"

namespace preview_frames = gui::components::configs::preview_frames;

namespace {
	// how long to wait between blur renders, so holding a slider doesn't start one per ui frame
	constexpr auto DEBOUNCE_TIME = std::chrono::milliseconds(50);

	// identifies what a frame is a frame of. once this stops matching the current video/seek, whatever's on screen
	// is out of date
	struct FrameKey {
		std::filesystem::path video_path;
		float seek = 0.f;

		// only the mask frame uses this, and it uses it in place of the seek - a mask is worked out from the whole
		// video, so every position in it has the same mask, and it's the settings that change which mask that is.
		// see mask_generation
		size_t mask_generation = 0;

		bool operator==(const FrameKey& other) const = default;
	};

	// a frame rendered on a background thread and uploaded to a texture on the render thread. publish() is the only
	// part called from anywhere else - everything below it is render thread only
	class PreviewFrame {
	public:
		explicit PreviewFrame(std::string name) : m_name(std::move(name)) {}

		// background thread
		void publish(std::vector<uint8_t> jpeg, const FrameKey& key) {
			std::lock_guard lock(m_mutex);

			m_pending_jpeg = std::move(jpeg);
			m_pending_key = key;
		}

		// textures can only be created on the render thread, so this needs calling before using the frame
		void upload() {
			std::vector<uint8_t> jpeg;
			FrameKey key;

			{
				std::lock_guard lock(m_mutex);

				jpeg = std::move(m_pending_jpeg);
				m_pending_jpeg.clear();
				key = m_pending_key;
			}

			if (auto texture = render::texture_from_jpeg(jpeg)) {
				m_texture = std::move(texture);
				m_jpeg = std::move(jpeg);
				m_key = key;

				// tells ui::add_image the texture behind the element has changed
				m_image_id = std::format("{} {}", m_name, ++m_id);
			}
		}

		void clear() {
			{
				std::lock_guard lock(m_mutex);

				m_pending_jpeg.clear();
				m_pending_key = {};
			}

			m_texture.reset();
			m_jpeg.clear();
			m_key = {};
			m_image_id.clear();
			// m_id deliberately keeps counting - ui::add_image reuses the texture it has when the id matches, so an
			// id can never be handed out twice
		}

		[[nodiscard]] bool valid() const {
			return m_texture && m_texture->is_valid();
		}

		[[nodiscard]] const FrameKey& key() const {
			return m_key;
		}

		[[nodiscard]] const std::shared_ptr<render::Texture>& texture() const {
			return m_texture;
		}

		[[nodiscard]] const std::string& image_id() const {
			return m_image_id;
		}

		[[nodiscard]] const std::vector<uint8_t>& jpeg() const {
			return m_jpeg;
		}

	private:
		std::string m_name;

		std::mutex m_mutex; // guards the pending frame only
		std::vector<uint8_t> m_pending_jpeg;
		FrameKey m_pending_key;

		std::shared_ptr<render::Texture> m_texture;
		std::vector<uint8_t> m_jpeg;
		FrameKey m_key;
		size_t m_id = 0;
		std::string m_image_id;
	};

	enum class PreviewKind {
		blurred,
		mask,
	};

	struct LastRender {
		FrameKey key;
		BlurSettings settings;
		std::chrono::steady_clock::time_point time;
	};

	struct PreviewSlot {
		PreviewSlot(PreviewKind kind, std::string name) : kind(kind), frame(std::move(name)) {}

		PreviewKind kind;
		PreviewFrame frame;
		LastRender last_render;
	};

	// kept apart so switching views can immediately show the last frame rendered for each
	PreviewSlot blurred_preview(PreviewKind::blurred, "blurred");
	PreviewSlot mask_preview(PreviewKind::mask, "mask");

	// bumped whenever the settings stop agreeing with what the mask on screen was rendered from, which is what
	// makes that mask frame stale. only the mask needs this: everything that changes the blurred frame is in its
	// own settings comparison below
	size_t mask_generation = 0;
	BlurSettings mask_generation_settings;

	struct ActiveRender {
		std::shared_ptr<rendering::RenderState> state;
		PreviewSlot* preview;
	};

	// the newest render owns publication; anything it replaced has been stopped and is ignored when it exits
	std::mutex render_mutex;
	std::optional<ActiveRender> active_render;

	// an unblurred frame from the source video, shown in place of the blurred preview until that catches up
	PreviewFrame source_frame("source");

	// only one extraction runs at a time. newer requests replace the queued one, so dragging the seek bar doesn't
	// pile up ffmpeg processes
	struct QueuedFrame {
		FrameKey key;
		float timestamp = 0.f;
	};

	struct {
		std::mutex mutex;
		std::optional<QueuedFrame> queued;
		std::optional<FrameKey> last_requested;
		bool running = false;
	} source_frame_worker;

	// read in the background - the seek bar needs the duration, and turning a seek into a timestamp needs the fps
	struct {
		std::mutex mutex;
		std::filesystem::path path;
		u::VideoInfo info;
	} video;

	void fetch_video_info(const std::filesystem::path& path) {
		std::thread([path] {
			auto info = u::get_video_info(path);

			std::lock_guard lock(video.mutex);

			// the sample video changed while we were reading this one
			if (video.path != path)
				return;

			video.info = info;
		}).detach();
	}

	float video_duration() {
		std::lock_guard lock(video.mutex);
		return video.info.duration;
	}

	// where in the source video the blurred preview for this seek will land. the source frame has to match it,
	// otherwise it'd jump the moment the blurred frame arrives
	std::optional<float> preview_timestamp(const BlurSettings& settings, float seek) {
		std::lock_guard lock(video.mutex);

		if (!video.info.has_video_stream)
			return {};

		return rendering::get_preview_frame_timestamp(settings, video.info, seek);
	}

	// stops the current render and claims publication for a new one. if the interrupted render was for the other
	// view, invalidate its request so switching back starts it again
	std::shared_ptr<rendering::RenderState> start_render(PreviewSlot& preview) {
		std::lock_guard lock(render_mutex);

		if (active_render) {
			active_render->state->stop();

			if (active_render->preview != &preview)
				active_render->preview->last_render = {};
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

	// starts a render of the requested view unless that request is already cached or in flight
	void render_preview_frame(const preview_frames::Request& request, const FrameKey& key, PreviewSlot& preview) {
		bool mask = preview.kind == PreviewKind::mask;

		// no point rendering positions a drag is only passing through, they'd be thrown away on the next mouse move
		if (request.seeking)
			return;

		// the mask's key carries its generation, which is everything about the settings the mask depends on, so
		// for that one the key answers for the settings as well
		if (preview.last_render.key == key && (mask || preview.last_render.settings == request.settings))
			return;

		auto now = std::chrono::steady_clock::now();

		if (now - preview.last_render.time < DEBOUNCE_TIME)
			return;

		u::log(mask ? "generating mask preview" : "generating config preview");

		// claimed here rather than on the worker, otherwise two renders started back to back could register in the
		// opposite order and let the older one publish over the newer
		auto state = start_render(preview);
		preview.last_render = { .key = key, .settings = request.settings, .time = now };

		std::thread([state,
		             &preview,
		             mask,
		             video_path = request.video_path,
		             settings = request.settings,
		             app_settings = request.app_settings,
		             key] {
			auto res = rendering::render_frame(video_path, settings, app_settings, state, key.seek, mask);

			// held across the publish - otherwise a newer render could register between the check and publication
			std::lock_guard lock(render_mutex);

			if (!active_render || active_render->state != state)
				return;

			if (res) {
				preview.frame.publish(std::move(res->frame_jpeg), key);
				u::log(mask ? "mask preview finished rendering" : "config preview finished rendering");
			}
			else {
				gui::components::notifications::show_failure_notification(
					mask ? "Failed to generate mask preview." : "Failed to generate config preview.",
					res.error(),
					std::chrono::duration<float>(10.f)
				);
			}

			active_render.reset();
		}).detach();
	}

	struct RenderStatus {
		bool rendering = false;
		bool analysing_mask = false;
	};

	RenderStatus render_status(const PreviewSlot& preview) {
		std::lock_guard lock(render_mutex);

		if (!active_render || active_render->preview != &preview)
			return {};

		return {
			.rendering = true,
			.analysing_mask =
				active_render->state->get_progress().init_stage == rendering::RenderState::InitStage::generating_mask,
		};
	}

	void extract_source_frame(const FrameKey& key, float timestamp) {
		std::lock_guard lock(source_frame_worker.mutex);

		if (source_frame_worker.last_requested == key)
			return;

		source_frame_worker.last_requested = key;
		source_frame_worker.queued = QueuedFrame{ .key = key, .timestamp = timestamp };

		if (source_frame_worker.running)
			return; // the running worker will pick this up when it's done with the current frame

		source_frame_worker.running = true;

		std::thread([] {
			while (true) {
				QueuedFrame next;

				{
					std::lock_guard lock(source_frame_worker.mutex);

					if (!source_frame_worker.queued) {
						source_frame_worker.running = false;
						return;
					}

					next = *source_frame_worker.queued;
					source_frame_worker.queued.reset();
				}

				auto jpeg = u::get_video_frame_jpeg(next.key.video_path, next.timestamp);

				if (!jpeg.empty())
					source_frame.publish(std::move(jpeg), next.key);
			}
		}).detach();
	}
}

preview_frames::Result preview_frames::update(const Request& request) {
	bool video_changed = false;
	{
		std::lock_guard lock(video.mutex);
		video_changed = video.path != request.video_path;
	}

	if (video_changed) {
		// whatever's on screen is a frame of the old video
		reset();

		{
			std::lock_guard lock(video.mutex);
			video.path = request.video_path;
		}

		if (!request.video_path.empty())
			fetch_video_info(request.video_path); // outside the lock, it wants it itself
	}

	if (request.video_path.empty())
		return {};

	FrameKey current{ .video_path = request.video_path, .seek = request.app_settings.config_preview_seek };

	// worked out whichever image is on screen, so a mask rendered before a settings change is known to be stale by
	// the time it's switched back to
	if (!config_blur::same_masking(mask_generation_settings, request.settings)) {
		mask_generation_settings = request.settings;
		mask_generation++;
	}

	// no seek: the mask is worked out from the whole video, so it's the same wherever the seek bar is
	FrameKey mask_current{ .video_path = request.video_path, .mask_generation = mask_generation };

	// anything the background threads finished has to be turned into a texture here, on the render thread
	blurred_preview.frame.upload();
	mask_preview.frame.upload();
	source_frame.upload();

	PreviewSlot& preview = request.show_mask ? mask_preview : blurred_preview;
	render_preview_frame(request, request.show_mask ? mask_current : current, preview);
	auto status = render_status(preview);

	Result result{
		.rendering = status.rendering,
		.analysing_mask = status.analysing_mask,
		.video_duration = video_duration(),
	};

	const PreviewFrame* frame;
	bool up_to_date;

	if (request.show_mask) {
		frame = &mask_preview.frame;
		up_to_date = frame->valid() && frame->key() == mask_current;
	}
	else {
		// seeking to a position the blur pipeline hasn't reached yet - grab the frame from the source video instead,
		// so seeking is instant rather than seek -> wait for a render -> seek again
		if (request.seeking) {
			if (auto timestamp = preview_timestamp(request.settings, current.seek))
				extract_source_frame(current, *timestamp);
		}

		up_to_date = blurred_preview.frame.valid() && blurred_preview.frame.key() == current;
		bool show_source = !up_to_date && source_frame.valid() && source_frame.key().video_path == current.video_path;
		frame = show_source ? &source_frame : &blurred_preview.frame;
	}

	// keep the last frame from this video visible, faded, until the requested one replaces it
	if (frame->valid() && frame->key().video_path == current.video_path) {
		result.frame = Frame{
			.texture = frame->texture(),
			.image_id = frame->image_id(),
			.up_to_date = up_to_date && !result.rendering,
		};
	}

	return result;
}

std::vector<uint8_t> preview_frames::current_mask_jpeg() {
	return mask_preview.frame.jpeg();
}

void preview_frames::reset() {
	stop_render();

	{
		std::lock_guard lock(source_frame_worker.mutex);

		source_frame_worker.queued.reset();
		source_frame_worker.last_requested.reset();
	}

	blurred_preview.frame.clear();
	mask_preview.frame.clear();
	source_frame.clear();

	blurred_preview.last_render = {};
	mask_preview.last_render = {};

	{
		std::lock_guard lock(video.mutex);

		video.path.clear();
		video.info = {};
	}
}
