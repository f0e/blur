#pragma once

namespace rendering {
	// Thread-safe render state shared between the UI thread (which issues
	// pause/stop and reads progress) and the render threads (which report
	// progress and preview frames). All mutable state is private; the two
	// sides only ever touch it through the methods below.
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

		// -- control (called from the UI thread) --

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

		[[nodiscard]] bool is_paused() const {
			std::lock_guard lock(m_mutex);
			return m_paused;
		}

		[[nodiscard]] Progress get_progress() const {
			std::lock_guard lock(m_mutex);
			return m_progress;
		}

		// -- pipeline-facing (called from the render threads) --

		[[nodiscard]] bool wants_stop() const {
			return m_to_stop;
		}

		[[nodiscard]] bool wants_pause() const {
			return m_to_pause;
		}

		// reflect the OS-level suspend state; resets fps tracking when pausing
		void mark_paused(bool paused) {
			std::lock_guard lock(m_mutex);
			m_paused = paused;
			if (paused) {
				m_progress.fps_initialised = false;
				m_progress.fps = 0.f;
			}
		}

		// fold a vspipe "Frame: n/m" update into progress + the status string
		void report_frame_progress(int current_frame, int total_frames);

		// -- preview frames (jpeg piped out of ffmpeg) --

		void enable_preview_capture() {
			m_read_stdout_jpg = true;
		}

		[[nodiscard]] bool preview_capture_enabled() const {
			return m_read_stdout_jpg;
		}

		void set_preview_jpeg(std::vector<uint8_t> jpeg) {
			std::lock_guard lock(m_preview_mutex);
			m_preview_jpeg = std::move(jpeg);
		}

		[[nodiscard]] std::vector<uint8_t> take_preview_jpeg() {
			std::lock_guard lock(m_preview_mutex);
			return std::exchange(m_preview_jpeg, {});
		}

	private:
		mutable std::mutex m_mutex;
		Progress m_progress;
		bool m_paused = false;

		std::atomic<bool> m_to_pause = false;
		std::atomic<bool> m_to_stop = false;

		std::atomic<bool> m_read_stdout_jpg = false;
		mutable std::mutex m_preview_mutex;
		std::vector<uint8_t> m_preview_jpeg;
	};
}
