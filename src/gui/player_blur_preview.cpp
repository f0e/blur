#include "player_blur_preview.h"
#include "ui/helpers/video.h"

PreviewState PlayerBlurPreview::update(const Request& request) {
	if (m_pre_render) {
		auto settings = pre_render_settings(request);
		bool moved = !request.player.is_paused() || request.player.get_time_pos() != m_pre_render->player_time;

		if (moved || request.video_path != m_pre_render->video_path)
			cancel_pre_render();
		else if (settings != m_pre_render->settings)
			rerender(request, settings);
		else
			m_pre_render->new_settings.reset();
	}

	if (m_pre_render_requested && request.player.is_paused() && request.player.seek_settled()) {
		m_pre_render_requested = false;
		begin_pre_render(request);
	}

	auto state = update_preview(request);

	if (m_pre_render)
		update_pre_render(request, state);

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
		state.frame = ui::Frame{ .player = ready };
	}
	else if (auto previous = m_preview.previous_player();
	         previous && request.player.frame_count() == m_player_frames_at_blur)
	{
		state.frame = ui::Frame{ .player = previous };
	}

	return state;
}

void PlayerBlurPreview::start_pre_render() {
	m_pre_render_requested = true;
}

PlayerBlurPreview::PreRenderSettings PlayerBlurPreview::pre_render_settings(const Request& request) {
	return {
		.blur = request.settings,
		.output_encoding = request.app_settings.pre_render_output_encoding,
		.fully_blur_first_frame = request.app_settings.fully_blur_first_frame,
	};
}

std::unique_ptr<PreRender> PlayerBlurPreview::make_pre_render(
	const Request& request, size_t start_frame, size_t end_frame
) {
	return std::make_unique<PreRender>(PreRender::Request{
		.video_path = request.video_path,
		.video_info = request.video_info,
		.settings = request.settings,
		.app_settings = request.app_settings,
		.start_frame = start_frame,
		.end_frame = end_frame,
		.volume = request.volume,
	});
}

void PlayerBlurPreview::begin_pre_render(const Request& request) {
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

	m_pre_render = ActivePreRender{
		.render = make_pre_render(request, start_frame, end_frame),
		.video_path = request.video_path,
		.settings = pre_render_settings(request),
		.player_time = *time,
	};
}

void PlayerBlurPreview::rerender(const Request& request, const PreRenderSettings& settings) {
	auto& active = *m_pre_render;
	auto now = std::chrono::steady_clock::now();

	if (settings != active.new_settings) {
		active.new_settings = settings;
		active.new_settings_since = now;
		return;
	}

	if (now - active.new_settings_since < SETTLE_TIME)
		return;

	// a looping one's rendered again for as far as it got
	const auto& render = *active.render;
	auto end_frame = render.looped_end_frame().value_or(render.end_frame());

	active.render = make_pre_render(request, render.start_frame(), end_frame);
	active.settings = settings;
	active.new_settings.reset();
}

void PlayerBlurPreview::update_pre_render(const Request& request, PreviewState& state) {
	auto& render = *m_pre_render->render;
	render.update();

	if (auto error = render.take_error())
		m_pre_render_error = std::move(error);

	if (render.failed()) {
		cancel_pre_render();
		return;
	}

	if (request.video_info.duration > 0.0) {
		auto [start, end] = render.source_range();
		state.pre_render_range = {
			static_cast<float>(start / request.video_info.duration),
			static_cast<float>(end / request.video_info.duration),
		};
	}

	if (auto player = render.player()) {
		state.frame = ui::Frame{ .player = player };
		state.pre_render_status = "looping the pre-render, space to stop";
		return;
	}

	// what's shown for where it started stays up until it's rendered a frame
	if (auto frame = render.frame())
		state.frame = ui::Frame{ .texture = frame, .texture_id = render.frame_id() };

	auto progress = render.progress();

	if (render.finishing())
		state.pre_render_status = "finishing the pre-render...";
	else if (progress.current_frame > 0)
		state.pre_render_status = std::format("pre-rendered {} frames, space to loop them", progress.current_frame);
	else
		state.pre_render_status = "starting a pre-render...";
}

bool PlayerBlurPreview::continue_pre_render() {
	if (!m_pre_render) {
		if (!m_pre_render_requested)
			return false;

		cancel_pre_render();
		return true;
	}

	if (!m_pre_render->render->finish())
		cancel_pre_render();

	return true;
}

void PlayerBlurPreview::cancel_pre_render() {
	m_pre_render_requested = false;
	m_pre_render.reset();
}

std::optional<rendering::RenderError> PlayerBlurPreview::take_error() {
	if (auto error = m_preview.take_error())
		return error;

	return std::exchange(m_pre_render_error, std::nullopt);
}

void PlayerBlurPreview::handle_event(const SDL_Event& event, bool& to_render) {
	m_preview.handle_event(event, to_render);

	if (m_pre_render)
		m_pre_render->render->handle_event(event, to_render);
}

std::optional<float> PlayerBlurPreview::player_position(const VideoPlayer& player, const media::VideoInfo& video_info) {
	auto time = player.get_time_pos();
	if (!time || video_info.video_duration <= 0.0)
		return std::nullopt;

	// mpv's clock starts with the container, which can be before the video's first frame
	double video_time = *time - (video_info.video_start_time - video_info.start_time);

	return static_cast<float>(std::clamp(video_time / video_info.video_duration, 0.0, 1.0));
}
