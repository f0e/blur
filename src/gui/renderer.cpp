
#include "renderer.h"

#include "common/rendering.h"
#include "common/rendering_frame.h"

#include "drag_handler.h"
#include "tasks.h"
#include "gui.h"

#include "ui/ui.h"
#include "ui/utils.h"
#include "ui/render.h"

#include "resources/fonts/eb_garamond.h"
#include "resources/fonts/dejavu_sans.h"

#define DEBUG_RENDER 0

const int PAD_X = 24;
const int PAD_Y = PAD_X;

const int NOTIFICATIONS_PAD_X = 10;
const int NOTIFICATIONS_PAD_Y = 10;

const float FPS_SMOOTHING = 0.95f;

void gui::renderer::init_fonts() {
	fonts::font = utils::create_font_from_data(DejaVuSans_ttf.data(), DejaVuSans_ttf.size(), 11);

	fonts::header_font = utils::create_font_from_data(
		EBGaramond_VariableFont_wght_ttf.data(), EBGaramond_VariableFont_wght_ttf.size(), 30
	);
	fonts::smaller_header_font = fonts::header_font;
	fonts::smaller_header_font.setSize(18.f);
}

void gui::renderer::set_cursor(os::NativeCursor cursor) {
	if (current_cursor != cursor) {
		current_cursor = cursor;
		window->setCursor(cursor);
	}

	set_cursor_this_frame = true;
}

void gui::renderer::components::render(
	ui::Container& container,
	const Render& render,
	bool current,
	float delta_time,
	bool& is_progress_shown,
	float& bar_percent
) {
	// todo: ui concept
	// screen start|      [faded]last_video current_video [faded]next_video next_video2 next_video3 (+5) |
	// screen end animate sliding in as it moves along the queue
	ui::add_text(
		std::format("video {} name text", render.get_render_id()),
		container,
		base::to_utf8(render.get_video_name()),
		gfx::rgba(255, 255, 255, (current ? 255 : 100)),
		fonts::smaller_header_font,
		os::TextAlign::Center
	);

	if (current)
		ui::add_spacing(container, 5);

	if (!current)
		return;

	auto render_status = render.get_status();
	int bar_width = 300;

	std::string preview_path = render.get_preview_path().string();
	if (!preview_path.empty() && render_status.current_frame > 0) {
		auto element = ui::add_image(
			"preview image",
			container,
			preview_path,
			gfx::Size(container.rect.w, container.rect.h / 2),
			std::to_string(render_status.current_frame)
		);
		if (element) {
			bar_width = (*element)->rect.w;
		}
	}

	if (render_status.init) {
		float render_progress = (float)render_status.current_frame / (float)render_status.total_frames;
		bar_percent = u::lerp(bar_percent, render_progress, 5.f * delta_time, 0.005f);

		ui::add_bar(
			"progress bar",
			container,
			bar_percent,
			gfx::rgba(51, 51, 51, 255),
			gfx::rgba(255, 255, 255, 255),
			bar_width,
			std::format("{:.1f}%", render_progress * 100),
			gfx::rgba(255, 255, 255, 255),
			&fonts::font
		);

		auto old_line_height = container.line_height; // todo: push/pop line height
		container.line_height = 9;
		ui::add_text(
			"progress text",
			container,
			std::format("frame {}/{}", render_status.current_frame, render_status.total_frames),
			gfx::rgba(255, 255, 255, 155),
			fonts::font,
			os::TextAlign::Center
		);
		ui::add_text(
			"progress text 2",
			container,
			std::format("{:.2f} frames per second", render_status.fps),
			gfx::rgba(255, 255, 255, 155),
			fonts::font,
			os::TextAlign::Center
		);
		container.line_height = old_line_height;

		is_progress_shown = true;
	}
	else {
		ui::add_text(
			"initialising render text",
			container,
			"Initialising render...",
			gfx::rgba(255, 255, 255, 255),
			fonts::font,
			os::TextAlign::Center
		);
	}
}

void gui::renderer::components::main_screen(ui::Container& container, float delta_time) {
	static float bar_percent = 0.f;

	if (rendering.get_queue().empty() && !current_render_copy) {
		bar_percent = 0.f;

		gfx::Point title_pos = container.rect.center();
		title_pos.y = int(PAD_Y + fonts::header_font.getSize());

		ui::add_text_fixed(
			"blur title text",
			container,
			title_pos,
			"blur",
			gfx::rgba(255, 255, 255, 255),
			fonts::header_font,
			os::TextAlign::Center
		);
		ui::add_button("open file button", container, "Open files", fonts::font, [] {
			base::paths paths;
			utils::show_file_selector("Blur input", "", {}, os::FileDialog::Type::OpenFiles, paths);
			tasks::add_files(paths);
		});
		ui::add_text(
			"drop file text",
			container,
			"or drop them anywhere",
			gfx::rgba(255, 255, 255, 255),
			fonts::font,
			os::TextAlign::Center
		);
	}
	else {
		bool is_progress_shown = false;

		rendering.lock();
		{
			auto current_render = rendering.get_current_render();

			// displays final state of the current render once where it would have been skipped otherwise
			auto render_current_edge_case = [&] {
				if (!current_render_copy)
					return;

				if (current_render) {
					if ((*current_render)->get_render_id() == (*current_render_copy).get_render_id()) {
						u::log("render final frame: wasnt deleted so just render normally");
						return;
					}
				}

				u::log("render final frame: it was deleted bro, rendering separately");

				components::render(container, *current_render_copy, true, delta_time, is_progress_shown, bar_percent);

				current_render_copy.reset();
			};

			render_current_edge_case();

			for (const auto [i, render] : u::enumerate(rendering.get_queue())) {
				bool current = current_render && render.get() == current_render.value();

				components::render(container, *render, current, delta_time, is_progress_shown, bar_percent);
			}
		}
		rendering.unlock();

		if (!is_progress_shown) {
			bar_percent = 0.f; // Reset when no progress bar is shown
		}
	}
}

void gui::renderer::components::configs::set_interpolated_fps() {
	if (interpolate_scale)
		settings.interpolated_fps = std::format("{}x", interpolated_fps_mult);
	else
		settings.interpolated_fps = std::to_string(interpolated_fps);
}

void gui::renderer::components::configs::options(ui::Container& container, BlurSettings& settings) {
	static const gfx::Color section_color = gfx::rgba(155, 155, 155, 255);

	bool first_section = true;
	auto section_component = [&](std::string label, bool* setting = nullptr) {
		if (!first_section) {
			ui::add_separator(std::format("section {} separator", label), container);
		}
		else
			first_section = false;

		if (setting) {
			ui::add_checkbox(std::format("section {}", label), container, label, *setting, fonts::font);
		}
		else {
			// ui::add_text(
			// 	std::format("section {}", label), container, label, gfx::rgba(255, 255, 255, 255), fonts::font
			// );
		}
	};

	/*
	    Blur
	*/
	section_component("blur", &settings.blur);

	if (settings.blur) {
		ui::add_slider("blur amount", container, 0.f, 2.f, &settings.blur_amount, "blur amount: {:.2f}", fonts::font);
		ui::add_slider("output fps", container, 1, 120, &settings.blur_output_fps, "output fps: {} fps", fonts::font);
	}

	/*
	    Interpolation
	*/
	section_component("interpolation", &settings.interpolate);

	if (settings.interpolate) {
		ui::add_checkbox(
			"interpolate scale checkbox",
			container,
			"interpolate by scaling fps",
			interpolate_scale,
			fonts::font,
			[&](bool new_value) {
				set_interpolated_fps();
			}
		);

		if (interpolate_scale) {
			ui::add_slider(
				"interpolated fps mult",
				container,
				1.f,
				10.f,
				&interpolated_fps_mult,
				"interpolated fps: {:.1f}x",
				fonts::font,
				[&](std::variant<int*, float*> value) {
					set_interpolated_fps();
				},
				0.1f
			);
		}
		else {
			ui::add_slider(
				"interpolated fps",
				container,
				1,
				2400,
				&interpolated_fps,
				"interpolated fps: {} fps",
				fonts::font,
				[&](std::variant<int*, float*> value) {
					set_interpolated_fps();
				}
			);
		}
	}

	/*
	    Rendering
	*/
	section_component("rendering");

	ui::add_slider(
		"quality",
		container,
		1,
		51,
		&settings.quality,
		"quality: {}",
		fonts::font,
		{},
		0.f,
		"(0 = lossless, 51 = horrible)"
	);

	ui::add_checkbox("deduplicate checkbox", container, "deduplicate", settings.deduplicate, fonts::font);

	ui::add_checkbox("preview checkbox", container, "preview", settings.preview, fonts::font);

	ui::add_checkbox(
		"detailed filenames checkbox", container, "detailed filenames", settings.detailed_filenames, fonts::font
	);

	/*
	    Timescale
	*/
	section_component("timescale", &settings.timescale);

	if (settings.timescale) {
		ui::add_slider(
			"input timescale",
			container,
			0.f,
			1.f,
			&settings.input_timescale,
			"input timescale: {:.2f}",
			fonts::font,
			{},
			0.01f
		);

		ui::add_slider(
			"output timescale",
			container,
			0.f,
			1.f,
			&settings.output_timescale,
			"output timescale: {:.2f}",
			fonts::font,
			{},
			0.01f
		);

		ui::add_checkbox(
			"adjust timescaled audio pitch checkbox",
			container,
			"adjust timescaled audio pitch",
			settings.output_timescale_audio_pitch,
			fonts::font
		);
	}

	/*
	    Filters
	*/
	section_component("filters", &settings.filters);

	if (settings.filters) {
		ui::add_slider(
			"brightness", container, 0.f, 2.f, &settings.brightness, "brightness: {:.2f}", fonts::font, {}, 0.01f
		);
		ui::add_slider(
			"saturation", container, 0.f, 2.f, &settings.saturation, "saturation: {:.2f}", fonts::font, {}, 0.01f
		);
		ui::add_slider("contrast", container, 0.f, 2.f, &settings.contrast, "contrast: {:.2f}", fonts::font, {}, 0.01f);
	}

	/*
	    Advanced Rendering
	*/
	section_component("advanced rendering");

	ui::add_checkbox(
		"gpu interpolation checkbox", container, "gpu interpolation", settings.gpu_interpolation, fonts::font
	);

	ui::add_checkbox("gpu rendering checkbox", container, "gpu rendering", settings.gpu_rendering, fonts::font);

	if (settings.gpu_rendering) {
		ui::add_dropdown(
			"gpu rendering dropdown",
			container,
			"gpu rendering - gpu type",
			{ "nvidia", "amd", "intel" },
			settings.gpu_type,
			fonts::font
		);
	}

	ui::add_text_input(
		"video container text input", container, settings.video_container, "video container", fonts::font
	);

	ui::add_text_input(
		"custom ffmpeg filters text input", container, settings.ffmpeg_override, "custom ffmpeg filters", fonts::font
	);

	ui::add_checkbox("debug checkbox", container, "debug", settings.debug, fonts::font);

	section_component("advanced blur");

	ui::add_slider(
		"blur weighting gaussian std dev slider",
		container,
		0.f,
		10.f,
		&settings.blur_weighting_gaussian_std_dev,
		"blur weighting gaussian std dev: {:.2f}",
		fonts::font
	);
	ui::add_checkbox(
		"blur weighting triangle reverse checkbox",
		container,
		"blur weighting triangle reverse",
		settings.blur_weighting_triangle_reverse,
		fonts::font
	);
	ui::add_text_input(
		"blur weighting bound input", container, settings.blur_weighting_bound, "blur weighting bound", fonts::font
	);

	/*
	    Advanced Interpolation
	*/
	section_component("advanced interpolation");

	static const std::vector<std::string> interpolation_presets = {
		"weak", "film", "smooth", "animation", "default", "test",
	};

	static const std::vector<std::string> interpolation_algorithms = {
		"1", "2", "11", "13", "21", "23",
	};

	static const std::vector<std::string> interpolation_block_sizes = { "4", "8", "16", "32" };

	ui::add_dropdown(
		"interpolation preset dropdown",
		container,
		"interpolation preset",
		interpolation_presets,
		settings.interpolation_preset,
		fonts::font
	);

	ui::add_dropdown(
		"interpolation algorithm dropdown",
		container,
		"interpolation algorithm",
		interpolation_algorithms,
		settings.interpolation_algorithm,
		fonts::font
	);

	ui::add_dropdown(
		"interpolation block size dropdown",
		container,
		"interpolation block size",
		interpolation_block_sizes,
		settings.interpolation_blocksize,
		fonts::font
	);

	ui::add_slider(
		"interpolation mask area slider",
		container,
		0,
		500,
		&settings.interpolation_mask_area,
		"interpolation mask area: {}",
		fonts::font
	);
}

// NOLINTBEGIN(readability-function-cognitive-complexity) todo: refactor
void gui::renderer::components::configs::preview(ui::Container& container, BlurSettings& settings) {
	static BlurSettings previewed_settings;
	static bool first = true;

	static auto debounce_time = std::chrono::milliseconds(50);
	auto now = std::chrono::steady_clock::now();
	static auto last_render_time = now;

	static size_t preview_id = 0;
	static std::filesystem::path preview_path;
	static bool loading = false;
	static std::mutex preview_mutex;

	auto sample_video_path = blur.settings_path / "sample_video.mp4";
	bool sample_video_exists = std::filesystem::exists(sample_video_path);

	auto render_preview = [&] {
		if (!sample_video_exists)
			return;

		if (first) {
			first = false;
		}
		else {
			if (settings == previewed_settings && !first)
				return;

			if (now - last_render_time < debounce_time)
				return;
		}

		u::log("generating config preview");

		previewed_settings = settings;
		last_render_time = now;

		{
			std::lock_guard<std::mutex> lock(preview_mutex);
			loading = true;
		}

		std::thread([sample_video_path, settings] {
			FrameRender* render = nullptr;

			{
				std::lock_guard<std::mutex> lock(render_mutex);

				// stop ongoing renders early, a new render's coming bro
				for (auto& render : renders) {
					render->stop();
				}

				render = renders.emplace_back(std::make_unique<FrameRender>()).get();
			}

			auto res = render->render(sample_video_path, settings);

			if (res.success) {
				std::lock_guard<std::mutex> lock(preview_mutex);
				preview_id++;

				Blur::remove_temp_path(preview_path.parent_path());

				preview_path = res.output_path;

				u::log("config preview finished rendering");
			}

			if (render == renders.back().get())
			{ // todo: this should be correct right? any cases where this doesn't work?
				loading = false;

				if (!res.success) {
					if (res.error_message != "Input path does not exist")
						add_notification("Failed to generate config preview", ui::NotificationType::NOTIF_ERROR);
				}
			}

			render->set_can_delete();
		}).detach();
	};

	render_preview();

	// remove finished renders
	{
		std::lock_guard<std::mutex> lock(render_mutex);
		std::erase_if(renders, [](const auto& render) {
			return render->can_delete();
		});
	}

	if (!preview_path.empty() && std::filesystem::exists(preview_path)) {
		auto element = ui::add_image(
			"config preview image",
			container,
			preview_path,
			container.rect.size(),
			std::to_string(preview_id),
			gfx::rgba(255, 255, 255, loading ? 100 : 255)
		);
	}
	else {
		if (sample_video_exists) {
			if (loading) {
				ui::add_text(
					"loading config preview text",
					container,
					"Loading config preview...",
					gfx::rgba(255, 255, 255, 100),
					fonts::font,
					os::TextAlign::Center
				);
			}
			else {
				ui::add_text(
					"failed to generate preview text",
					container,
					"Failed to generate preview.",
					gfx::rgba(255, 255, 255, 100),
					fonts::font,
					os::TextAlign::Center
				);
			}
		}
		else {
			ui::add_text(
				"sample video does not exist text",
				container,
				"Sample video does not exist",
				gfx::rgba(255, 255, 255, 100),
				fonts::font,
				os::TextAlign::Center
			);
		}
	}
}

// NOLINTEND(readability-function-cognitive-complexity)

void gui::renderer::components::configs::screen(
	ui::Container& config_container, ui::Container& preview_container, float delta_time
) {
	auto parse_interp = [&] {
		try {
			auto split = u::split_string(settings.interpolated_fps, "x");
			if (split.size() > 1) {
				interpolated_fps_mult = std::stof(split[0]);
				interpolate_scale = true;
			}
			else {
				interpolated_fps = std::stof(split[1]);
				interpolate_scale = false;
			}

			u::log(
				"loaded interp, scale: {} (fps: {}, mult: {})",
				interpolate_scale,
				interpolated_fps,
				interpolated_fps_mult
			);
		}
		catch (std::exception& e) {
			u::log("failed to parse interpolated fps, setting defaults cos user error");
			set_interpolated_fps();
		}
	};

	auto on_load = [&] {
		current_global_settings = settings;
		parse_interp();
	};

	auto save_config = [&] {
		config::create(config::get_global_config_path(), settings);
		current_global_settings = settings;

		u::log("saved global settings");
	};

	if (!loaded_config) {
		settings = config::parse_global_config();
		on_load();
		loaded_config = true;
	}

	bool config_changed = settings != current_global_settings;

	if (config_changed) {
		ui::set_next_same_line(nav_container);
		ui::add_button("save button", nav_container, "Save", fonts::font, [&] {
			save_config();
		});

		ui::set_next_same_line(nav_container);
		ui::add_button("reset changes button", nav_container, "Reset changes", fonts::font, [&] {
			settings = current_global_settings;
			on_load();
		});
	}

	if (settings != DEFAULT_SETTINGS) {
		ui::set_next_same_line(nav_container);
		ui::add_button("restore defaults button", nav_container, "Restore defaults", fonts::font, [&] {
			settings = BlurSettings();
			parse_interp();
		});
	}

	options(config_container, settings);

	preview(preview_container, settings);
}

// NOLINTBEGIN(readability-function-size,readability-function-cognitive-complexity)

bool gui::renderer::redraw_window(os::Window* window, bool force_render) {
	set_cursor_this_frame = false;

	auto now = std::chrono::steady_clock::now();
	static auto last_frame_time = now;

	// todo: first render in a batch might be fucked, look at progress bar skipping fully to complete instantly on
	// 25 speed - investigate
	static bool first = true;

#if DEBUG_RENDER
	float fps = -1.f;
#endif
	float delta_time = NAN;

	if (first) {
		delta_time = DEFAULT_DELTA_TIME;
		first = false;
	}
	else {
		float time_since_last_frame =
			std::chrono::duration<float>(std::chrono::steady_clock::now() - last_frame_time).count();

#if DEBUG_RENDER
		fps = 1.f / time_since_last_frame;

// float current_fps = 1.f / time_since_last_frame;
// if (fps == -1.f)
// 	fps = current_fps;
// fps = (fps * FPS_SMOOTHING) + (current_fps * (1.0f - FPS_SMOOTHING));
#endif

		delta_time = std::min(time_since_last_frame, MIN_DELTA_TIME);
	}

	last_frame_time = now;

	os::Surface* surface = window->surface();
	const gfx::Rect rect = surface->bounds();

	static float bg_overlay_shade = 0.f;
	float last_fill_shade = bg_overlay_shade;
	bg_overlay_shade = u::lerp(bg_overlay_shade, drag_handler::dragging ? 30.f : 0.f, 25.f * delta_time);
	force_render |= bg_overlay_shade != last_fill_shade;

	gfx::Rect container_rect = rect;
	container_rect.x += PAD_X;
	container_rect.w -= PAD_X * 2;
	container_rect.y += PAD_Y;
	container_rect.h -= PAD_Y * 2;

	gfx::Rect nav_container_rect = rect;
	nav_container_rect.h = 70;
	nav_container_rect.y = rect.y2() - nav_container_rect.h;

	ui::reset_container(nav_container, nav_container_rect, fonts::font.getSize());

	int nav_cutoff = container_rect.y2() - nav_container_rect.y;
	if (nav_cutoff > 0)
		container_rect.h -= nav_cutoff;

	ui::reset_container(main_container, container_rect, fonts::font.getSize());

	const int config_page_container_gap = PAD_X / 2;

	gfx::Rect config_container_rect = container_rect;
	config_container_rect.w = 200 + PAD_X * 2;

	ui::reset_container(config_container, config_container_rect, 9);

	gfx::Rect config_preview_container_rect = container_rect;
	config_preview_container_rect.x = config_container_rect.x2() + config_page_container_gap;
	config_preview_container_rect.w -= config_container_rect.w + config_page_container_gap;

	ui::reset_container(config_preview_container, config_preview_container_rect, fonts::font.getSize());

	gfx::Rect notification_container_rect = rect;
	notification_container_rect.w = 230;
	notification_container_rect.x = rect.x2() - notification_container_rect.w - NOTIFICATIONS_PAD_X;
	notification_container_rect.h = 300;
	notification_container_rect.y = NOTIFICATIONS_PAD_Y;

	ui::reset_container(notification_container, notification_container_rect, 6);

	switch (screen) {
		case Screens::MAIN: {
			components::main_screen(main_container, delta_time);

			ui::set_next_same_line(nav_container);
			ui::add_button("config button", nav_container, "Config", fonts::font, [] {
				screen = Screens::CONFIG;
			});

			ui::center_elements_in_container(main_container);

			components::configs::loaded_config = false;

			break;
		}
		case Screens::CONFIG: {
			ui::set_next_same_line(nav_container);
			ui::add_button("back button", nav_container, "Back", fonts::font, [] {
				screen = Screens::MAIN;
			});

			components::configs::screen(config_container, config_preview_container, delta_time);

			ui::center_elements_in_container(config_preview_container);

			break;
		}
	}

	render_notifications();

	ui::center_elements_in_container(nav_container);

	bool want_to_render = false;
	want_to_render |= ui::update_container_frame(notification_container, delta_time);
	want_to_render |= ui::update_container_frame(nav_container, delta_time);

	want_to_render |= ui::update_container_frame(main_container, delta_time);
	want_to_render |= ui::update_container_frame(config_container, delta_time);
	want_to_render |= ui::update_container_frame(config_preview_container, delta_time);
	ui::on_update_frame_end();

	if (!want_to_render && !force_render)
		// note: DONT RENDER ANYTHING ABOVE HERE!!! todo: render queue?
		return false;

	// background
	render::rect_filled(surface, rect, gfx::rgba(0, 0, 0, 255));

#if DEBUG_RENDER
	{
		// debug
		static const int debug_box_size = 30;
		static float x = rect.x2() - debug_box_size;
		static float y = 100.f;
		static bool right = false;
		static bool down = true;
		x += 1.f * (right ? 1 : -1);
		y += 1.f * (down ? 1 : -1);

		render::rect_filled(surface, gfx::Rect(x, y, debug_box_size, debug_box_size), gfx::rgba(255, 0, 0, 50));

		if (right) {
			if (x + debug_box_size > rect.x2())
				right = false;
		}
		else {
			if (x < 0)
				right = true;
		}

		if (down) {
			if (y + debug_box_size > rect.y2())
				down = false;
		}
		else {
			if (y < 0)
				down = true;
		}
	}
#endif

	ui::render_container(surface, main_container);
	ui::render_container(surface, config_container);
	ui::render_container(surface, config_preview_container);
	ui::render_container(surface, nav_container);
	ui::render_container(surface, notification_container);

	// file drop overlay
	if ((int)bg_overlay_shade > 0)
		render::rect_filled(surface, rect, gfx::rgba(255, 255, 255, (gfx::ColorComponent)bg_overlay_shade));

#if DEBUG_RENDER
	if (fps != -1.f) {
		gfx::Point fps_pos(rect.x2() - PAD_X, rect.y + PAD_Y);
		render::text(
			surface,
			fps_pos,
			gfx::rgba(0, 255, 0, 255),
			std::format("{:.0f} fps", fps),
			fonts::font,
			os::TextAlign::Right
		);
	}
#endif

	// todo: whats this do
	if (!window->isVisible())
		window->setVisible(true);

	window->invalidateRegion(gfx::Region(rect));

	return want_to_render;
}

// NOLINTEND(readability-function-size,readability-function-cognitive-complexity)

void gui::renderer::add_notification(
	const std::string& text, ui::NotificationType type, std::chrono::steady_clock::time_point end_time
) {
	std::lock_guard<std::mutex> lock(notification_mutex);

	notifications.emplace_back(Notification{
		.end_time = end_time,
		.text = text,
		.type = type,
	});
}

void gui::renderer::add_notification(
	const std::string& text, ui::NotificationType type, std::chrono::duration<float> duration
) {
	add_notification(
		text,
		type,
		std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration)
	);
}

void gui::renderer::on_render_finished(Render* render, bool success) {
	add_notification(
		std::format("Render '{}' {}", base::to_utf8(render->get_video_name()), success ? "finished" : "failed"),
		success ? ui::NotificationType::SUCCESS : ui::NotificationType::NOTIF_ERROR
	);
}

void gui::renderer::render_notifications() {
	std::lock_guard<std::mutex> lock(notification_mutex);

	auto now = std::chrono::steady_clock::now();

	for (auto it = notifications.begin(); it != notifications.end();) {
		ui::add_notification(
			std::format("notification {}", it->id), notification_container, it->text, it->type, fonts::font
		);

		if (now > it->end_time)
			it = notifications.erase(it);
		else
			++it;
	}
}
