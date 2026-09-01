#include "main.h"
#include "../renderer.h"

#include "common/rendering.h"

#include "../tasks.h"
#include "../gui.h"

#include "../ui/ui.h"
#include "common/masks.h"
#include "../render/render.h"
#include <SDL3/SDL_dialog.h>

namespace main = gui::components::main;

namespace {
	size_t pending_index = 0;

	const std::string NO_CONFIG_OPTION = "select a config";

	// keyed on the video and the config it's set to, not the video alone - switching a video to a config
	// whose preset copies the audio has to re-answer this, and it's the config that decides the answer
	std::map<std::pair<size_t, std::string>, bool> trim_disabled_cache;

	bool is_trim_disabled(const tasks::PendingVideo& pending_video) {
		if (!pending_video.video_info)
			return false;

		auto key = std::pair{ pending_video.video_id, pending_video.config_name };

		auto it = trim_disabled_cache.find(key);
		if (it != trim_disabled_cache.end())
			return it->second;

		bool disabled = false;

		if (!pending_video.video_info->audio_sample_rates.empty()) {
			auto app_settings = config_app::get_app_config();

			auto config = config_blur::get_config(pending_video.config_name);

			disabled = rendering::detail::copies_audio(config, app_settings);
		}

		trim_disabled_cache.emplace(key, disabled);
		return disabled;
	}
}

void main::invalidate_trim_support() {
	trim_disabled_cache.clear();
}

void main::open_files_button(ui::Container& container, const std::string& label) {
	ui::add_button("open file button", container, label, fonts::dejavu, [] {
		static auto file_callback = [](void* userdata, const char* const* files, int filter) {
			if (files && *files) {
				std::vector<std::filesystem::path> wpaths;

				for (const char* const* f = files; *f != nullptr; ++f) {
					wpaths.emplace_back(u::string_to_path(*f));
				}

				tasks::add_files(wpaths);
			}
		};

		const SDL_DialogFileFilter filters[] = {
			{ "Video files",
			  "webm;mkv;flv;vob;ogv;ogg;rrc;gifv;mng;mov;avi;qt;wmv;yuv;rm;rmvb;asf;amv;mp4;m4p;m4v;mpg;mp2;mpeg;mpe;"
			  "mpv;svi;3gp;3g2;mxf;roq;nsv;f4v;f4p;f4a;f4b;mod;ts;m2ts;mts;divx;bik;wtv;drc" }
		};

		SDL_ShowOpenFileDialog(
			file_callback, // Properly typed callback function
			nullptr,       // userdata
			nullptr,       // parent window (nullptr for default)
			filters,       // file filters
			1,             // number of filters
			"",            // default path
			true           // allow multiple files
		);
	});
};

void main::render_progress(
	ui::Container& container,
	const rendering::VideoRenderDetails& render,
	size_t render_index,
	bool current,
	float delta_time,
	bool& is_progress_shown,
	float& bar_percent
) {
	// todo: ui concept
	// screen start|      [faded]last_video current_video [faded]next_video next_video2 next_video3 (+5) |
	// screen end animate sliding in as it moves along the queue

	std::string render_title_text = u::path_to_string(render.input_path.stem());

	if (current) {
		int queue_size = rendering::video_render_queue.size() + tasks::finished_renders;
		if (queue_size > 1) {
			render_title_text = std::format("{} ({}/{})", render_title_text, tasks::finished_renders + 1, queue_size);
		}
	}

	ui::add_text(
		std::format("video {} name text", render_index),
		container,
		render_title_text,
		gfx::Color(255, 255, 255, (current ? 255 : 100)),
		fonts::garamond(fonts::size::SMALL_HEADER),
		FONT_CENTERED_X
	);

	if (!current)
		return;

	int bar_width = 300;

	static std::shared_ptr<render::Texture> preview_texture;
	static std::weak_ptr<rendering::RenderState> preview_texture_state;
	static size_t preview_generation = 0;

	if (preview_texture_state.lock() != render.state) {
		preview_texture.reset();
		preview_texture_state = render.state;
		preview_generation++;
	}

	// create texture from current render preview (here cause its main thread)
	if (auto texture = render::texture_from_jpeg(render.state->take_preview_jpeg()))
		preview_texture = std::move(texture);

	auto progress = render.state->get_progress();

	if (preview_texture && progress.current_frame > 0) {
		auto element = ui::add_image(
			"preview image",
			container,
			preview_texture,
			gfx::Size(container.get_usable_rect().w, container.get_usable_rect().h / 2),
			// add_image caches by image id, and frame numbers restart every render
			std::format("{}:{}", preview_generation, progress.current_frame)
		);
		if (element) {
			bar_width = (*element)->element->rect.w;
		}
	}
	if (progress.rendered_a_frame) {
		float render_progress = (float)progress.current_frame / (float)progress.total_frames;
		bar_percent = u::lerp(bar_percent, render_progress, 5.f * delta_time, 0.005f);

		ui::add_bar(
			"progress bar",
			container,
			bar_percent,
			gfx::Color(51, 51, 51, 255),
			gfx::Color::white(),
			bar_width,
			std::format("{:.1f}%", render_progress * 100),
			gfx::Color::white(),
			fonts::dejavu
		);

		container.push_element_gap(6);

		if (render.state->is_paused()) {
			ui::add_text(
				"paused text",
				container,
				"Paused",
				gfx::Color::white(renderer::MUTED_SHADE),
				fonts::dejavu,
				FONT_CENTERED_X
			);
		}

		bool status_fps_init = progress.fps != 0.f;

		if (!status_fps_init)
			container.pop_element_gap();

		ui::add_text(
			"progress text",
			container,
			std::format("frame {}/{}", progress.current_frame, progress.total_frames),
			gfx::Color::white(renderer::MUTED_SHADE),
			fonts::dejavu,
			FONT_CENTERED_X
		);

		if (status_fps_init) {
			ui::add_text(
				"progress text fps",
				container,
				std::format("{:.2f} frames per second", progress.fps),
				gfx::Color::white(renderer::MUTED_SHADE),
				fonts::dejavu,
				FONT_CENTERED_X
			);

			container.pop_element_gap();

			int remaining_frames = progress.total_frames - progress.current_frame;
			int eta_seconds = static_cast<int>(remaining_frames / progress.fps);

			int hours = eta_seconds / 3600;
			int minutes = (eta_seconds % 3600) / 60;
			int seconds = eta_seconds % 60;

			std::ostringstream eta_stream;
			if (hours > 0)
				eta_stream << hours << " hour" << (hours > 1 ? "s " : " ");
			if (minutes > 0)
				eta_stream << minutes << " minute" << (minutes > 1 ? "s " : " ");
			if (seconds > 0 || (hours == 0 && minutes == 0))
				eta_stream << seconds << " second" << (seconds != 1 ? "s" : "");

			ui::add_text(
				"progress text eta",
				container,
				std::format("~{} left", eta_stream.str()),
				gfx::Color::white(renderer::MUTED_SHADE),
				fonts::dejavu,
				FONT_CENTERED_X
			);
		}

		is_progress_shown = true;
	}
	else {
		if (render.state->is_paused()) {
			ui::add_text(
				"paused text",
				container,
				"Paused",
				gfx::Color::white(renderer::MUTED_SHADE),
				fonts::dejavu,
				FONT_CENTERED_X
			);
		}
		else {
			ui::add_spinner("initialising render spinner", container, 8.f, gfx::Color::white(50));

			std::string initialising_text = "Initialising render...";
			switch (progress.init_stage) {
				case rendering::RenderState::InitStage::generating_mask:
					initialising_text = "Analysing video to generate a mask...";
					break;
				case rendering::RenderState::InitStage::building_engine:
					initialising_text = "Building TensorRT engine. This may take a few minutes...";
					break;
				case rendering::RenderState::InitStage::none:
					break;
			}

			ui::add_text(
				"initialising render text",
				container,
				initialising_text,
				gfx::Color::white(),
				fonts::dejavu,
				FONT_CENTERED_X
			);
		}
	}
}

void main::render_pending(
	ui::Container& container,
	ui::Container& config_container,
	ui::Container& queue_container,
	const std::vector<std::shared_ptr<tasks::PendingVideo>>& pending
) {
	pending_index = (size_t)std::clamp((int)pending_index, 0, int(pending.size() - 1));

	auto& pending_video = pending[pending_index];

	std::string render_title_text = u::path_to_string(pending_video->video_path.stem());

	int queue_size = pending.size() + tasks::finished_renders;
	if (queue_size > 1) {
		render_title_text = std::format("{} ({}/{})", render_title_text, pending_index + 1, queue_size);
	}

	const auto title_font = fonts::garamond(fonts::size::SMALL_HEADER);

	ui::add_text_fixed(
		std::format("video {} name text", pending_index),
		container,
		container.get_usable_rect().top_center(),
		render_title_text,
		gfx::Color::white(),
		title_font,
		FONT_CENTERED_X | FONT_DROPSHADOW | FONT_OUTLINE
	);

	std::vector<ui::UIVideo> ui_videos;

	ui_videos.reserve(pending.size());
	for (const auto& pv : pending) {
		ui_videos.push_back(
			{
				.video_id = pv->video_id,
				.path = pv->video_path,
				.video_info = pv->video_info,
				.start = &pv->start,
				.end = &pv->end,
				.trim_disabled = is_trim_disabled(*pv),
			}
		);
	}

	auto app_config = config_app::get_app_config();

	float volume = app_config.preview_volume; // todo: make a gui element for controlling it? idk

	bool trim_disabled = is_trim_disabled(*pending_video);

	ui::add_videos(
		"test video",
		queue_container,
		ui_videos,
		pending_index,
		pending_video->start,
		pending_video->end,
		volume,
		app_config.preview_hardware_decoding,
		trim_disabled,
		[&](size_t removed_video_id) {
			tasks::cancel_pending(removed_video_id);
		}
	);

	{
		// the dropdown holds onto a pointer to this, so it has to outlive the frame
		static std::string selected_config;
		selected_config = pending_video->config_name.empty() ? NO_CONFIG_OPTION : pending_video->config_name;

		std::optional<std::string> config_missing_message;
		if (pending_video->config_missing_warning && pending_video->config_name.empty())
			config_missing_message = "you need to select a config";

		ui::add_with_message(
			config_container,
			std::format("config missing warning {}", pending_video->video_id),
			config_missing_message,
			gfx::Color(255, 100, 100),
			[&] {
				ui::add_dropdown(
					std::format("config dropdown {}", pending_video->video_id),
					config_container,
					"config",
					config_blur::options(pending_video->config_name),
					selected_config,
					fonts::dejavu,
					[pending_video](std::string* new_value) {
						if (pending_video->config_name == *new_value)
							return;

						pending_video->config_name = *new_value;
						pending_video->config_missing_warning = false;

						// picked by hand, so the line below stops explaining where it came from
						pending_video->config_source = config_blur::ConfigSource::OVERRIDE;
						pending_video->config_rule_pattern.clear();

						auto config = config_blur::get_config(pending_video->config_name);
						pending_video->mask = config.mask;
						pending_video->auto_mask = config.auto_mask;

						// whether trimming is allowed depends on the config's encode preset
						invalidate_trim_support();
					},
					// show the placeholder as muted without making it selectable
					{ NO_CONFIG_OPTION }
				);
			}
		);

		// say where the config came from, so a rule quietly picking one isn't a surprise
		std::optional<std::string> config_reason;

		switch (pending_video->config_source) {
			case config_blur::ConfigSource::DEFAULT:
				config_reason = "using the default config";
				break;
			case config_blur::ConfigSource::RULE:
				config_reason = std::format("matching rule '{}'", pending_video->config_rule_pattern);
				break;
			default:
				break;
		}

		if (config_reason) {
			ui::add_text(
				std::format("config reason {}", pending_video->video_id),
				config_container,
				*config_reason,
				gfx::Color::white(renderer::MUTED_SHADE),
				fonts::dejavu(fonts::size::SMALL)
			);
		}

		// the dropdown holds onto a pointer to this, so it has to outlive the frame
		static std::string selected_mask;
		selected_mask = pending_video->mask.empty() ? masks::NONE_OPTION : pending_video->mask;

		// The retained elements own the callbacks below, so each video needs its own ids. Reusing one id would
		// leave the callback pointing at whichever video first created the element.
		ui::add_dropdown(
			std::format("mask dropdown {}", pending_video->video_id),
			config_container,
			"mask",
			masks::options(pending_video->mask),
			selected_mask,
			fonts::dejavu,
			[pending_video](std::string* new_value) {
				pending_video->mask = *new_value == masks::NONE_OPTION ? "" : *new_value;
			},
			{ masks::NONE_OPTION }
		);

		// the checkbox writes through this rather than straight into the clip, since the element outlives the
		// frame and the clip it belongs to is picked out again each one
		static bool auto_mask;
		auto_mask = pending_video->auto_mask;

		// stacks on top of the mask above rather than replacing it
		ui::add_checkbox(
			std::format("auto mask checkbox {}", pending_video->video_id),
			config_container,
			"auto mask",
			auto_mask,
			fonts::dejavu,
			[pending_video](bool new_value) {
				pending_video->auto_mask = new_value;
			},
			true
		);
	}

	if (trim_disabled) {
		ui::add_text(
			"trim disabled notice",
			queue_container,
			"Trimming unavailable, as the current preset copies the audio ('-c:a copy')",
			gfx::Color::white(renderer::MUTED_SHADE),
			fonts::dejavu,
			FONT_CENTERED_X
		);
	}

	if (pending_video->trim_range_warning) {
		if (pending_video->video_info && rendering::has_enough_frames_to_render(
											 *pending_video->video_info, pending_video->start, pending_video->end
										 ))
		{
			pending_video->trim_range_warning = false;
		}
		else {
			ui::add_text(
				"trim range warning",
				queue_container,
				"Trim will result in an empty video",
				gfx::Color(255, 100, 100),
				fonts::dejavu,
				FONT_CENTERED_X
			);
		}
	}
}

void main::render_home(ui::Container& container) {
	gfx::Point title_pos = container.get_usable_rect().center();
	if (container.rect.h > 275)
		title_pos.y = int(renderer::PAD_Y + fonts::garamond.height());
	else
		title_pos.y = 10 + fonts::garamond.height();

	ui::add_text_fixed(
		"blur title text", container, title_pos, "blur", gfx::Color::white(), fonts::garamond, FONT_CENTERED_X
	);

	if (!initialisation_res) {
		ui::add_text(
			"failed to initialise text",
			container,
			"Failed to initialise",
			gfx::Color::white(),
			fonts::dejavu,
			FONT_CENTERED_X
		);

		ui::add_text(
			"failed to initialise reason",
			container,
			initialisation_res.error(),
			gfx::Color::white(renderer::MUTED_SHADE),
			fonts::dejavu,
			FONT_CENTERED_X
		);

		return;
	}

	open_files_button(container, "Open files");

	ui::add_text(
		"drop file text", container, "or drop them anywhere", gfx::Color::white(), fonts::dejavu, FONT_CENTERED_X
	);
}

main::MainScreen main::screen(
	ui::Container& container, ui::Container& queue_config_container, ui::Container& queue_container, float delta_time
) {
	static float bar_percent = 0.f;

	auto app_config = config_app::get_app_config();

	const auto& pending = tasks::get_pending_copy();

	if (pending.size() > 0) {
		bool needs_config = std::ranges::any_of(pending, [](const auto& pending_video) {
			return pending_video->config_name.empty();
		});

		bool needs_trim_fix = std::ranges::any_of(pending, [](const auto& pending_video) {
			return pending_video->trim_range_warning;
		});

		if (!app_config.skip_queue || needs_config || needs_trim_fix) {
			render_pending(container, queue_config_container, queue_container, pending);
			return MainScreen::PENDING;
		}
	}
	else {
		pending_index = 0;
	}

	const auto& queue = rendering::video_render_queue.get_queue_copy();

	if (!queue.empty()) {
		bool is_progress_shown = false;

		for (const auto [i, render] : u::enumerate(queue)) {
			bool current = i == 0;

			render_progress(container, render, i, current, delta_time, is_progress_shown, bar_percent);
		}

		if (!is_progress_shown)
			bar_percent = 0.f;

		return MainScreen::PROGRESS;
	}

	bar_percent = 0.f;

	render_home(container);

	return MainScreen::HOME;
}
