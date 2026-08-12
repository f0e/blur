#pragma once

#include "render_state.h"
#include "render_types.h"
#include "common/config_app.h"
#include "common/config_blur.h"

namespace rendering {
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

		// pulls the front render off the queue, runs it to completion and fires its
		// finish callback. defined in the .cpp as it drives detail::render_video.
		bool process_next();

		void stop() {
			m_active = false;
			// now no more renders will start. see process_next.
		}

		// stop the queue and block until the in-flight render (if any) has finished
		void stop_and_wait();

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
}
