#include "render_state.h"

namespace {
	constexpr std::string_view STATUS_PREFIX = "[blur:status] ";

	constexpr std::string_view STAGE_KEY = "stage";
	constexpr std::string_view MASK_STAGE = "mask";              // blur/mask.py
	constexpr std::string_view ENGINE_STAGE = "tensorrt-engine"; // external/vsmlrt.py

	constexpr std::string_view FRAME_TIMING_LOG_KEY = "frame-timing-log";
}

void rendering::RenderState::report_log_line(const std::string& line) {
	auto at = line.find(STATUS_PREFIX);
	if (at == std::string::npos)
		return;

	std::string_view status = std::string_view(line).substr(at + STATUS_PREFIX.size());

	auto equals = status.find('=');
	if (equals == std::string_view::npos)
		return;

	auto key = status.substr(0, equals);
	auto value = status.substr(equals + 1);

	std::lock_guard lock(m_mutex);

	if (key == STAGE_KEY) {
		if (value == MASK_STAGE)
			m_progress.init_stage = InitStage::generating_mask;
		else if (value == ENGINE_STAGE)
			m_progress.init_stage = InitStage::building_engine;
	}
	else if (key == FRAME_TIMING_LOG_KEY) {
		m_progress.frame_timing_log = value;
	}
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
