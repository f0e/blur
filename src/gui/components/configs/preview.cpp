#include "configs.h"

#include "preview_frames.h"

#include "common/weighting.h"

#include "../../tasks.h"

#include "../../ui/ui.h"
#include "../../render/render.h"

namespace {
	// left over from the last ui frame - the seek bar is added after the preview image it affects
	bool seek_bar_dragging = false;

	void confirm_clear_sample_video() {
		ui::dialog::confirm_destructive("Remove sample video?", "", "Remove", [] {
			gui::components::configs::clear_sample_video();
		});
	}
}

namespace configs = gui::components::configs;

void configs::reset_config_preview() {
	preview_frames::reset();

	seek_bar_dragging = false;
}

bool configs::has_sample_video() {
	return !app_settings.sample_video_path.empty() &&
	       std::filesystem::exists(u::string_to_path(app_settings.sample_video_path));
}

void configs::set_sample_video(const std::filesystem::path& path) {
	app_settings.sample_video_path = u::path_to_string(path);
	save_preview_app_settings();
}

void configs::clear_sample_video() {
	app_settings.sample_video_path.clear();
	save_preview_app_settings();

	reset_config_preview();
}

void configs::save_preview_app_settings() {
	// read the config back off disk and only put the preview settings into it, so anything else the user has
	// changed is still theirs to save or discard with the buttons
	auto saved_settings = config_app::get_app_config();
	saved_settings.sample_video_path = app_settings.sample_video_path;
	saved_settings.config_preview_seek = app_settings.config_preview_seek;

	config_app::create(config_app::get_app_config_path(), saved_settings);

	current_app_settings.sample_video_path = app_settings.sample_video_path;
	current_app_settings.config_preview_seek = app_settings.config_preview_seek;
}

void configs::config_preview(ui::Container& container) {
	// from the last ui frame - the seek bar is added further down. cleared here so it can't get stuck on if the seek
	// bar stops being drawn mid-drag
	bool dragging_seek_bar = seek_bar_dragging;
	seek_bar_dragging = false;

	auto sample_video_path = u::string_to_path(app_settings.sample_video_path);
	bool sample_video_set = !app_settings.sample_video_path.empty();
	bool sample_video_exists = sample_video_set && std::filesystem::exists(sample_video_path);

	// an unusable video goes in as an empty path, which tears the preview down
	auto preview_video_path = sample_video_exists ? sample_video_path : std::filesystem::path{};

	auto preview = preview_frames::update(
		{
			.video_path = preview_video_path,
			.settings = settings,
			.app_settings = app_settings,
			.seeking = dragging_seek_bar,
		}
	);

	auto add_open_sample_video_prompt = [&](bool was_deleted) {
		ui::add_text(
			"drop sample video text",
			container,
			was_deleted ? "Drop a video here to add a new one" : "Drop a video here to add one.",
			gfx::Color::white(100),
			fonts::dejavu,
			FONT_CENTERED_X
		);

		ui::add_button("open preview file button", container, "Or open file", fonts::dejavu, [] {
			static auto file_callback = [](void* userdata, const char* const* files, int filter) {
				if (files != nullptr && *files != nullptr) {
					const char* file = *files;
					tasks::add_sample_video(u::string_to_path(file));
				}
			};

			const std::array filters = {
				SDL_DialogFileFilter{
					.name = "Video files",
					.pattern = "webm;mkv;flv;vob;ogv;ogg;rrc;gifv;mng;mov;avi;qt;wmv;yuv;rm;rmvb;asf;amv;mp4;m4p;m4v;"
							   "mpg;mp2;mpeg;mpe;mpv;svi;3gp;3g2;mxf;roq;nsv;f4v;f4p;f4a;f4b;mod;ts;m2ts;mts;divx;"
							   "bik;wtv;drc",
				},
			};

			SDL_ShowOpenFileDialog(file_callback, nullptr, nullptr, filters.data(), 0, "", false);
		});
	};

	if (!sample_video_set) {
		ui::add_text(
			"no sample video text",
			container,
			"No preview video found.",
			gfx::Color::white(100),
			fonts::dejavu,
			FONT_CENTERED_X
		);

		add_open_sample_video_prompt(false);
		return;
	}

	if (!sample_video_exists) {
		ui::add_text(
			"missing sample video text",
			container,
			std::format("Preview video ({}) no longer exists.", u::path_to_string(sample_video_path.filename())),
			gfx::Color::white(100),
			fonts::dejavu,
			FONT_CENTERED_X
		);

		ui::add_button("remove old sample video button", container, "Clear sample video", fonts::dejavu, [] {
			confirm_clear_sample_video();
		});

		add_open_sample_video_prompt(true);

		return;
	}

	if (preview.frame) {
		container.push_element_gap(2);

		// anything that isn't the finished blurred frame for the current settings is faded out
		ui::add_image(
			"config preview image",
			container,
			preview.frame->texture,
			container.get_usable_rect().size(),
			preview.frame->image_id,
			gfx::Color::white(preview.frame->up_to_date ? 255 : 100)
		);

		container.pop_element_gap();

		container.push_element_gap(DELETE_ICON_GAP);
		{
			int seek_bar_height = ui::seek_bar_height(fonts::dejavu_small);

			auto* seek_bar = ui::add_seek_bar(
				"config preview seek bar",
				container,
				app_settings.config_preview_seek,
				fonts::dejavu_small,
				preview.video_duration,
				container.get_usable_rect().w - seek_bar_height - DELETE_ICON_GAP
			);

			seek_bar_dragging = ui::get_active_element() == seek_bar;

			// save once the drag is over rather than writing the config on every frame it moves
			if (app_settings.config_preview_seek != current_app_settings.config_preview_seek && !seek_bar_dragging) {
				save_preview_app_settings();
			}

			ui::set_next_same_line(container);

			ui::add_icon_button(
				"remove sample video button",
				container,
				DELETE_ICON,
				fonts::icons,
				gfx::Size(seek_bar_height, seek_bar_height),
				DELETE_ICON_COLOR,
				DELETE_ICON_HOVER_COLOR,
				[] {
					confirm_clear_sample_video();
				},
				"Remove sample video"
			);
		}
		container.pop_element_gap();
	}
	else if (preview.rendering) {
		ui::add_text(
			"loading config preview text",
			container,
			"Loading config preview...",
			gfx::Color::white(100),
			fonts::dejavu,
			FONT_CENTERED_X
		);
	}
	else {
		ui::add_text(
			"failed to generate preview text",
			container,
			"Failed to generate preview.",
			gfx::Color::white(100),
			fonts::dejavu,
			FONT_CENTERED_X
		);
	}
}

// todo: refactor
void configs::preview(ui::Container& header_container, ui::Container& content_container) {
	std::optional<int> interp_fps;
	if (settings.interpolate) {
		std::istringstream iss(settings.interpolated_fps);
		int temp_fps{};
		if ((iss >> temp_fps) && iss.eof()) {
			interp_fps = temp_fps;
		}
	}

	auto on_tab_select = [&content_container] {
		content_container.scroll_to_top = true;
		old_tab.clear();
	};

	ui::add_tabs("preview tab", header_container, TABS, selected_tab, fonts::dejavu, on_tab_select);

	if (selected_tab == TABS[0]) {
		config_preview(content_container);
	}
	else if (selected_tab == TABS[1]) {
		auto weight_settings = settings;

		if (!hovered_weighting.empty())
			weight_settings.blur_weighting = hovered_weighting;

		auto weights_res = weighting::get_weights(weight_settings, interp_fps);
		if (weights_res.error.empty()) {
			ui::add_weighting_graph("weighting graph", content_container, weights_res.weights, interp_fps.has_value());
		}
		else {
			ui::add_text(
				"weighting error",
				content_container,
				weights_res.error,
				gfx::Color(255, 50, 50, 255),
				fonts::dejavu,
				FONT_CENTERED_X | FONT_OUTLINE
			);
		}
	}

	auto validation_res = config_blur::validate(settings, app_settings, preset_settings, false);
	std::string fixable_errors = validation_res.message(true);
	if (!fixable_errors.empty()) {
		ui::add_separator("config preview separator", content_container, ui::SeparatorStyle::FADE_BOTH);

		ui::add_button(
			"fix config button", content_container, "Reset invalid config options to defaults", fonts::dejavu, [] {
				config_blur::validate(settings, app_settings, preset_settings, true);
			}
		);
	}
}
