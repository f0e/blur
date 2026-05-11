#pragma once

#include "config_app.h"

struct RenderCommands {
	std::vector<std::string> vspipe_video;
	std::vector<std::string> ffmpeg;
};

namespace rendering {
	struct RenderResult {
		std::filesystem::path output_path;
		bool stopped = false;
	};

	using RenderError = u::ParsedError;

	struct RenderState;

	namespace detail {
		struct PipelineResult;

		void pause(int pid, const std::shared_ptr<RenderState>& state);
		void resume(int pid, const std::shared_ptr<RenderState>& state);

		tl::expected<PipelineResult, RenderError> execute_pipeline(
			const RenderCommands& commands,
			const std::shared_ptr<RenderState>& state,
			bool debug,
			bool audio,
			const std::function<void()>& progress_callback
		);

		tl::expected<RenderResult, std::variant<std::string, RenderError>> render_video(
			const std::filesystem::path& input_path,
			const u::VideoInfo& video_info,
			const BlurSettings& settings,
			const std::shared_ptr<RenderState>& state,
			const GlobalAppSettings& app_settings,
			const std::optional<std::filesystem::path>& output_path_override,
			float start,
			float end,
			const std::function<void()>& progress_callback
		);
	}

	// --

	struct RenderState {
		struct Progress {
			bool rendered_a_frame = false;
			int current_frame = 0;
			int total_frames = 0;

			bool fps_initialised = false;
			std::chrono::steady_clock::time_point start_time;
			int start_frame = 0;
			std::chrono::duration<double> elapsed_time;
			float fps = 0.f;

			std::string string;
		};

		void pause() {
			m_to_pause = true;
		}

		void resume() {
			m_to_pause = false;
		}

		void toggle_pause() {
			m_to_pause = !m_to_pause;
		}

		void stop() {
			m_to_stop = true;
		}

		bool is_paused() {
			std::lock_guard lock(m_mutex);
			return m_paused;
		}

		Progress get_progress() {
			std::lock_guard lock(m_mutex);
			return m_progress;
		}

		// friends can access non-thread-safe stuff
		friend void detail::pause(int pid, const std::shared_ptr<RenderState>& state);
		friend void detail::resume(int pid, const std::shared_ptr<RenderState>& state);

		friend tl::expected<detail::PipelineResult, RenderError> detail::execute_pipeline(
			const RenderCommands& commands,
			const std::shared_ptr<RenderState>& state,
			bool debug,
			bool audio,
			const std::function<void()>& progress_callback
		);

		friend tl::expected<RenderResult, std::variant<std::string, RenderError>> detail::render_video(
			const std::filesystem::path& input_path,
			const u::VideoInfo& video_info,
			const BlurSettings& settings,
			const std::shared_ptr<RenderState>& state,
			const GlobalAppSettings& app_settings,
			const std::optional<std::filesystem::path>& output_path_override,
			float start,
			float end,
			const std::function<void()>& progress_callback
		);

	public:
		// TODO: shitcode but getters are bloat
		bool m_read_stdout_jpg = false;
		std::mutex m_preview_mutex;
		std::vector<uint8_t> m_preview_jpeg;

		std::mutex m_mutex;

		Progress m_progress;
		bool m_paused = false;

		std::atomic<bool> m_to_pause = false;
		std::atomic<bool> m_to_stop = false;
	};

	struct VideoRenderDetails {
		std::filesystem::path input_path;
		u::VideoInfo video_info;
		BlurSettings settings;
		GlobalAppSettings app_settings;
		std::optional<std::filesystem::path> output_path_override;

		float start;
		float end;

		std::function<void()> progress_callback;
		std::function<void(
			const VideoRenderDetails& render,
			const tl::expected<rendering::RenderResult, std::variant<std::string, RenderError>>& result
		)>
			finish_callback;

		std::shared_ptr<RenderState> state = std::make_shared<RenderState>();
	};

	namespace detail {
		struct PipelineResult {
			bool stopped;
		};

		tl::expected<nlohmann::json, std::string> merge_settings(
			const BlurSettings& blur_settings, const GlobalAppSettings& app_settings
		);

		std::vector<std::string> build_vspipe_args(
			const std::filesystem::path& input_path, const nlohmann::json& merged_settings
		);

		boost::process::native_environment setup_environment();

		tl::expected<std::filesystem::path, std::string> build_output_filename(
			const std::filesystem::path& input_path, const BlurSettings& settings, const GlobalAppSettings& app_settings
		);

		void copy_file_timestamp(const std::filesystem::path& from, const std::filesystem::path& to);
	}

	struct QueueAddRes {
		bool is_global_config;
		std::optional<std::string> error;
		std::shared_ptr<rendering::RenderState> state;
	};

	class VideoRenderQueue {
	public:
		QueueAddRes add(
			const std::filesystem::path& input_path,
			const u::VideoInfo& video_info,
			const std::optional<std::filesystem::path>& config_path = {},
			const GlobalAppSettings& app_settings = config_app::get_app_config(),
			const std::optional<std::filesystem::path>& output_path_override = {},
			float start = 0.f,
			float end = 1.f,
			const std::function<void()>& progress_callback = {},
			const std::function<void(
				const VideoRenderDetails& render,
				const tl::expected<rendering::RenderResult, std::variant<std::string, RenderError>>& result
			)>& finish_callback = {}
		);

		bool process_next() {
			if (m_queue.empty() || !m_active)
				return false;

			auto cur = m_queue.front();

			auto res = detail::render_video(
				cur.input_path,
				cur.video_info,
				cur.settings,
				cur.state,
				cur.app_settings,
				cur.output_path_override,
				cur.start,
				cur.end,
				cur.progress_callback
			);

			if (cur.finish_callback)
				cur.finish_callback(cur, res);

			std::unique_lock lock(m_mutex);
			m_queue.erase(m_queue.begin());

			return true;
		}

		void stop() {
			m_active = false;
			// now no more renders will start. see process_next.
		}

		void stop_and_wait() {
			stop();

			std::lock_guard lock(m_mutex);
			if (m_queue.empty())
				return;

			// still rendering the video at the front, so tell it to stop
			auto cur = m_queue.front();
			cur.state->stop();

			while (!is_empty()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		bool is_empty() const {
			std::lock_guard lock(m_mutex);
			return m_queue.empty();
		}

		size_t size() const {
			std::lock_guard lock(m_mutex);
			return m_queue.size();
		}

		std::optional<VideoRenderDetails> front() {
			std::lock_guard lock(m_mutex);
			if (m_queue.empty())
				return {};
			return m_queue.front();
		}

		std::vector<VideoRenderDetails> get_queue_copy() {
			return m_queue;
		}

	private:
		std::vector<VideoRenderDetails> m_queue;
		mutable std::mutex m_mutex;
		std::atomic<bool> m_active = true;
	};

	inline VideoRenderQueue video_render_queue;

	struct FrameRenderResult {
		std::vector<uint8_t> frame_jpeg;
		bool stopped = false;
	};

	tl::expected<FrameRenderResult, std::variant<std::string, RenderError>> render_frame(
		const std::filesystem::path& input_path,
		const BlurSettings& settings,
		const GlobalAppSettings& app_settings = config_app::get_app_config(),
		const std::shared_ptr<RenderState>& state = std::make_shared<RenderState>()
	);
}
