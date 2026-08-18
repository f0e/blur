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

	private:
		std::string m_name;

		std::mutex m_mutex; // guards the pending frame only
		std::vector<uint8_t> m_pending_jpeg;
		FrameKey m_pending_key;

		std::shared_ptr<render::Texture> m_texture;
		FrameKey m_key;
		size_t m_id = 0;
		std::string m_image_id;
	};

	// the real preview - the sample video put through the blur pipeline
	PreviewFrame blurred_frame("blurred");
	std::atomic<bool> blur_render_running = false;

	// the render backing the newest preview request. anything older has been stopped and isn't allowed
	// to publish its result anymore
	std::mutex current_render_mutex;
	std::shared_ptr<rendering::RenderState> current_render_state;

	// what the last blur render was started for, so we're not rendering the same thing over and over
	struct {
		FrameKey key;
		BlurSettings settings;
		std::chrono::steady_clock::time_point time;
	} last_blur_render;

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

	// stops whatever render is in flight and makes state the current one, so only its result is allowed through
	std::shared_ptr<rendering::RenderState> set_current_render(std::shared_ptr<rendering::RenderState> state) {
		std::lock_guard lock(current_render_mutex);

		if (current_render_state)
			current_render_state->stop();

		current_render_state = std::move(state);

		return current_render_state;
	}

	void render_blurred_frame(const preview_frames::Request& request, const FrameKey& key) {
		// no point rendering positions a drag is only passing through, they'd be thrown away on the next mouse move
		if (request.seeking)
			return;

		if (last_blur_render.key == key && last_blur_render.settings == request.settings)
			return;

		auto now = std::chrono::steady_clock::now();

		if (now - last_blur_render.time < DEBOUNCE_TIME)
			return;

		u::log("generating config preview");

		last_blur_render = { .key = key, .settings = request.settings, .time = now };

		blur_render_running = true;

		// claimed here rather than on the worker, otherwise two renders started back to back could register in the
		// opposite order and let the older one publish over the newer
		auto state = set_current_render(std::make_shared<rendering::RenderState>());

		std::thread([state,
		             video_path = request.video_path,
		             settings = request.settings,
		             app_settings = request.app_settings,
		             key] {
			auto res = rendering::render_frame(video_path, settings, app_settings, state, key.seek);

			// held across the publish - checking first and publishing after would let a newer render register in
			// between and take this stale result on top of its own
			std::lock_guard lock(current_render_mutex);

			// the result is for stale settings, throw it away. the render that replaced this one owns the flag now
			if (current_render_state != state)
				return;

			if (res) {
				blurred_frame.publish(std::move(res->frame_jpeg), key);

				u::log("config preview finished rendering");
			}
			else {
				gui::components::notifications::show_failure_notification(
					"Failed to generate config preview.", res.error(), std::chrono::duration<float>(10.f)
				);
			}

			// after publishing, so the ui never sees "not rendering" with nothing new to show
			blur_render_running = false;
		}).detach();
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

	// anything the background threads finished has to be turned into a texture here, on the render thread
	blurred_frame.upload();
	source_frame.upload();

	render_blurred_frame(request, current);

	// seeking to a position the blur pipeline hasn't reached yet - grab the frame from the source video instead, so
	// seeking is instant rather than seek -> wait for a render -> seek again
	if (request.seeking) {
		if (auto timestamp = preview_timestamp(request.settings, current.seek))
			extract_source_frame(current, *timestamp);
	}

	bool blurred_up_to_date = blurred_frame.valid() && blurred_frame.key() == current;
	bool show_source_frame =
		!blurred_up_to_date && source_frame.valid() && source_frame.key().video_path == current.video_path;

	const PreviewFrame& frame = show_source_frame ? source_frame : blurred_frame;

	Result result{
		.rendering = blur_render_running,
		.video_duration = video_duration(),
	};

	if (frame.valid()) {
		result.frame = Frame{
			.texture = frame.texture(),
			.image_id = frame.image_id(),
			.up_to_date = blurred_up_to_date && !blur_render_running,
		};
	}

	return result;
}

void preview_frames::reset() {
	set_current_render(nullptr);

	{
		std::lock_guard lock(source_frame_worker.mutex);

		source_frame_worker.queued.reset();
		source_frame_worker.last_requested.reset();
	}

	blurred_frame.clear();
	source_frame.clear();

	blur_render_running = false;
	last_blur_render = {};

	{
		std::lock_guard lock(video.mutex);

		video.path.clear();
		video.info = {};
	}
}
