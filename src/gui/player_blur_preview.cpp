#include "player_blur_preview.h"
#include "ui/helpers/video.h"

PreviewState PlayerBlurPreview::update(const Request& request) {
	if (m_sample) {
		bool moved = !request.player.is_paused() || request.player.get_time_pos() != m_sample->player_time;

		if (moved || request.video_path != m_sample->video_path)
			cancel_sample();
		else if (request.settings != m_sample->settings)
			rerender_sample(request);
		else
			m_sample->new_settings.reset();
	}

	if (m_sample_requested && request.player.is_paused() && request.player.seek_settled()) {
		m_sample_requested = false;
		begin_sample(request);
	}

	auto state = update_preview(request);

	if (m_sample)
		update_sample(state);

	return state;
}

PreviewState PlayerBlurPreview::update_preview(const Request& request) {
	// the blurred preview's kept loaded while playing, so pausing again only has to seek
	if (!request.player.is_paused())
		return { .playing = true };

	// mpv gives a seek's target as the position straight away, so the blurred frame renders alongside the seek
	auto position = player_position(request.player, request.video_info);
	if (!position)
		return { .status = m_preview.status() };

	m_preview.update(
		{
			.video_path = request.video_path,
			.video_info = request.video_info,
			.settings = request.settings,
			.app_settings = request.app_settings,
			.position = *position,
		}
	);

	PreviewState state{ .status = m_preview.status() };

	if (auto ready = m_preview.ready_player()) {
		m_player_frames_at_blur = request.player.frame_count();
		state.overlay = ready;
	}
	else if (request.player.frame_count() == m_player_frames_at_blur) {
		state.overlay = m_preview.previous_player();
	}

	return state;
}

void PlayerBlurPreview::start_sample() {
	m_sample_requested = true;
}

void PlayerBlurPreview::begin_sample(const Request& request) {
	const auto& info = request.video_info;

	auto time = request.player.get_time_pos();
	auto position = player_position(request.player, info);
	if (!time || !position || info.fps_num <= 0 || info.fps_den <= 0)
		return;

	double fps = static_cast<double>(info.fps_num) / info.fps_den;
	auto start_frame = static_cast<size_t>(std::llround(*position * info.video_duration * fps));
	auto end_frame = static_cast<size_t>(std::llround(info.video_duration * fps));

	if (end_frame <= start_frame)
		return;

	m_sample = Sample{
		.video_path = request.video_path,
		.settings = request.settings,
		.player_time = *time,
		.start_frame = start_frame,
		.end_frame = end_frame,
		.sample = std::make_unique<BlurSample>(BlurSample::Request{
			.video_path = request.video_path,
			.video_info = info,
			.settings = request.settings,
			.app_settings = request.app_settings,
			.start_frame = start_frame,
			.end_frame = end_frame,
			.volume = request.volume,
		}),
	};
}

void PlayerBlurPreview::rerender_sample(const Request& request) {
	auto& sample = *m_sample;
	auto now = std::chrono::steady_clock::now();

	if (request.settings != sample.new_settings) {
		sample.new_settings = request.settings;
		sample.new_settings_since = now;
		return;
	}

	if (now - sample.new_settings_since < BlurSample::SETTLE_TIME)
		return;

	// a looping one's rendered again for as far as it got
	auto end_frame = sample.sample->looped_end_frame().value_or(sample.end_frame);

	sample.settings = request.settings;
	sample.new_settings.reset();
	sample.sample = std::make_unique<BlurSample>(BlurSample::Request{
		.video_path = request.video_path,
		.video_info = request.video_info,
		.settings = request.settings,
		.app_settings = request.app_settings,
		.start_frame = sample.start_frame,
		.end_frame = end_frame,
		.volume = request.volume,
	});
}

void PlayerBlurPreview::update_sample(PreviewState& state) {
	auto& sample = *m_sample->sample;
	sample.update();

	if (auto error = sample.take_error())
		m_sample_error = std::move(error);

	if (sample.failed()) {
		cancel_sample();
		return;
	}

	if (auto player = sample.player()) {
		state.overlay = player;
		state.sample_status = "looping the blurred sample, space to stop";
		return;
	}

	state.sample_frame = sample.frame();
	state.sample_frame_id = sample.frame_id();

	auto progress = sample.progress();

	if (sample.finishing())
		state.sample_status = "finishing the blurred sample...";
	else if (progress.current_frame > 0)
		state.sample_status = std::format("rendered {} blurred frames, space to loop them", progress.current_frame);
	else
		state.sample_status = "starting a blurred sample...";
}

bool PlayerBlurPreview::continue_sample() {
	if (!m_sample) {
		if (!m_sample_requested)
			return false;

		cancel_sample();
		return true;
	}

	if (!m_sample->sample->finish())
		cancel_sample();

	return true;
}

void PlayerBlurPreview::cancel_sample() {
	m_sample_requested = false;
	m_sample.reset();
}

std::optional<rendering::RenderError> PlayerBlurPreview::take_error() {
	if (auto error = m_preview.take_error())
		return error;

	return std::exchange(m_sample_error, std::nullopt);
}

void PlayerBlurPreview::handle_event(const SDL_Event& event, bool& to_render) {
	m_preview.handle_event(event, to_render);

	if (m_sample)
		m_sample->sample->handle_event(event, to_render);
}

std::optional<float> PlayerBlurPreview::player_position(const VideoPlayer& player, const media::VideoInfo& video_info) {
	auto time = player.get_time_pos();
	if (!time || video_info.video_duration <= 0.0)
		return std::nullopt;

	// mpv's clock starts with the container, which can be before the video's first frame
	double video_time = *time - (video_info.video_start_time - video_info.start_time);

	return static_cast<float>(std::clamp(video_time / video_info.video_duration, 0.0, 1.0));
}
