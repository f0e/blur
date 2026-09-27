#include "blur_sample.h"
#include "blur_preview.h"
#include "ui/helpers/video.h"
#include "render/render.h"

#include "common/rendering/render.h"

namespace {
	// fewer and ffmpeg might not have a whole frame to keep
	constexpr int MIN_FRAMES = 2;

	// ids stay unique across samples, since the ui caches images by them
	size_t last_frame_id = 0;

	// samples that were still open when they ended
	std::vector<std::filesystem::path> old_samples;

	void remove_old_samples() {
		std::erase_if(old_samples, [](const std::filesystem::path& path) {
			std::error_code ec;
			std::filesystem::remove(path, ec);
			return !ec;
		});
	}
}

BlurSample::BlurSample(const Request& request)
	: m_path(BlurPreview::temp_file_path("mkv")), m_volume(request.volume),
	  m_hardware_decoding(request.app_settings.preview_hardware_decoding), m_start_frame(request.start_frame),
	  m_end_frame(request.end_frame),
	  m_fps(static_cast<double>(request.video_info.fps_num) / request.video_info.fps_den),
	  m_clock_offset(request.video_info.video_start_time - request.video_info.start_time) {
	const auto& settings = request.settings;

	m_speed = settings.timescale ? static_cast<double>(settings.output_timescale) / settings.input_timescale : 1.0;

	std::promise<tl::expected<rendering::RenderResult, rendering::RenderError>> promise;
	m_render = promise.get_future();

	u::log("rendering blur sample for {}", u::path_to_string(request.video_path));

	// detached so ending the sample doesn't wait for the render to stop
	std::thread([promise = std::move(promise),
	             video_path = request.video_path,
	             video_info = request.video_info,
	             settings = request.settings,
	             app_settings = request.app_settings,
	             state = m_state,
	             path = m_path,
	             start_frame = request.start_frame,
	             end_frame = request.end_frame]() mutable {
		auto result = rendering::render_sample(
			video_path, video_info, settings, app_settings, state, path, start_frame, end_frame
		);

		if (!result || result->stopped) {
			std::error_code ec;
			std::filesystem::remove(path, ec);
		}

		promise.set_value(std::move(result));
	}).detach();
}

BlurSample::~BlurSample() {
	// the render removes its own file if it's stopped
	m_state->stop();

	// the ui can still be holding on to the player, so the file might not be free to remove yet
	m_player.reset();
	old_samples.push_back(m_path);

	remove_old_samples();
}

void BlurSample::update() {
	remove_old_samples();

	if (m_render.valid() && m_render.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		auto result = m_render.get();

		if (!result || result->stopped) {
			if (!result) {
				u::log_error("blur sample failed: {}", result.error().to_string());
				m_error = result.error();
			}

			m_failed = true;
			return;
		}

		m_player = std::make_shared<VideoPlayer>(m_volume, m_hardware_decoding);
		m_player->load_file(m_path, {}, { { "loop-file", "inf" } });
		m_player->set_paused(false);
	}

	if (player()) {
		m_frame.reset();
		return;
	}

	if (auto frame = render::texture_from_jpeg(m_state->take_preview_jpeg())) {
		m_frame = std::move(frame);
		m_frame_id = ++last_frame_id;
	}
}

bool BlurSample::finish() {
	if (m_player || m_failed || m_state->get_progress().current_frame < MIN_FRAMES)
		return false;

	m_state->finish();
	return true;
}

bool BlurSample::finishing() const {
	return m_state->wants_finish() || (m_player && !player());
}

std::optional<size_t> BlurSample::looped_end_frame() const {
	auto looping = player();
	if (!looping)
		return {};

	auto duration = looping->get_duration();
	if (!duration)
		return {};

	auto frames = static_cast<size_t>(std::llround(*duration * m_speed * m_fps));
	return std::min(m_start_frame + frames, m_end_frame);
}

std::pair<double, double> BlurSample::source_range() const {
	auto end_frame = looped_end_frame();

	if (!end_frame) {
		auto progress = m_state->get_progress();
		double rendered =
			progress.total_frames > 0 ? static_cast<double>(progress.current_frame) / progress.total_frames : 0.0;

		end_frame = m_start_frame + static_cast<size_t>(std::llround((m_end_frame - m_start_frame) * rendered));
	}

	auto seconds = [&](size_t frame) {
		return m_clock_offset + (static_cast<double>(frame) / m_fps);
	};

	return { seconds(m_start_frame), seconds(*end_frame) };
}

std::shared_ptr<VideoPlayer> BlurSample::player() const {
	if (!m_player || !m_player->has_frame() || !m_player->get_video_dimensions())
		return nullptr;

	return m_player;
}

void BlurSample::handle_event(const SDL_Event& event, bool& to_render) {
	if (m_player)
		m_player->handle_mpv_event(event, to_render, true);
}
