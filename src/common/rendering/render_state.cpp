#include "render_state.h"

namespace {
	// printed by vsmlrt.py right before it shells out to trtexec/tensorrt_rtx
	constexpr std::string_view ENGINE_BUILD_SENTINEL = "[blur] Building TensorRT engine";

	// printed by blur/mask.py before it reads through the video to work out an automatic mask
	constexpr std::string_view MASK_SENTINEL = "[blur] Generating mask";
}

void rendering::RenderState::report_log_line(const std::string& line) {
	InitStage stage{};

	if (line.find(ENGINE_BUILD_SENTINEL) != std::string::npos)
		stage = InitStage::building_engine;
	else if (line.find(MASK_SENTINEL) != std::string::npos)
		stage = InitStage::generating_mask;
	else
		return;

	std::lock_guard lock(m_mutex);
	m_progress.init_stage = stage;
}

void rendering::RenderState::report_frame_progress(int current_frame, int total_frames) {
	std::lock_guard lock(m_mutex);

	m_progress.current_frame = current_frame;
	m_progress.total_frames = total_frames;
	m_progress.rendered_a_frame = true;

	float progress = m_progress.current_frame / (float)m_progress.total_frames;

	if (!m_progress.fps_initialised) {
		m_progress.fps_initialised = true;
		m_progress.start_time = std::chrono::steady_clock::now();
		m_progress.start_frame = m_progress.current_frame;
		m_progress.fps = 0.f;

		m_progress.string =
			std::format("{:.1f}% complete ({}/{})", progress * 100, m_progress.current_frame, m_progress.total_frames);
	}
	else {
		auto current_time = std::chrono::steady_clock::now();
		m_progress.elapsed_time = current_time - m_progress.start_time;

		m_progress.fps = (m_progress.current_frame - m_progress.start_frame) / m_progress.elapsed_time.count();

		m_progress.string = std::format(
			"{:.1f}% complete ({}/{}, {:.2f} fps)",
			progress * 100,
			m_progress.current_frame,
			m_progress.total_frames,
			m_progress.fps
		);
	}

	u::log(m_progress.string);
}
