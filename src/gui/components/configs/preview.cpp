#include "configs.h"

#include "preview_frames.h"

#include "common/masks.h"
#include "common/weighting.h"

#include "../../tasks.h"

#include "../notifications.h"
#include "../../render/render.h"
#include "../../ui/ui.h"

namespace {
	// left over from the last ui frame - the seek bar is added after the preview image it affects
	bool seek_bar_dragging = false;

	struct SaveMaskDialogState {
		std::string name;
		std::string error;
	};

	tl::expected<std::filesystem::path, std::string> get_mask_preset_path(const std::string& entered_name) {
		auto valid_name = u::validate_filename(entered_name);
		if (!valid_name)
			return tl::unexpected(valid_name.error());

		std::string name = *valid_name;

		if (!u::to_lower(name).ends_with(".png"))
			name += ".png";

		auto path = masks::get_path() / u::string_to_path(name);
		std::error_code ec;
		if (std::filesystem::exists(path, ec))
			return tl::unexpected("A mask preset with that name already exists.");

		return path;
	}

	tl::expected<std::filesystem::path, std::string> save_mask_preset(
		const std::vector<uint8_t>& jpeg, const std::string& name
	) {
		auto path = get_mask_preset_path(name);
		if (!path)
			return tl::unexpected(path.error());

		std::error_code ec;
		std::filesystem::create_directories(path->parent_path(), ec);
		if (ec)
			return tl::unexpected(std::format("Could not create the masks folder: {}", ec.message()));

		SDL_Surface* surface = render::jpeg_bytes_to_surface(jpeg.data(), jpeg.size());
		if (!surface)
			return tl::unexpected("Could not read the current mask preview.");

		bool saved = IMG_SavePNG(surface, u::path_to_string(*path).c_str());
		SDL_DestroySurface(surface);

		if (!saved)
			return tl::unexpected(std::format("Could not save the mask preset: {}", SDL_GetError()));

		return *path;
	}

	void open_save_mask_dialog(std::vector<uint8_t> jpeg) {
		auto state = std::make_shared<SaveMaskDialogState>();

		ui::dialog::open(
			{
				.title = "Save mask preset",
				.content =
					[state](ui::Container& container) {
						ui::add_text_input(
							"mask preset name input",
							container,
							state->name,
							"name",
							fonts::dejavu,
							"",
							[state](const std::string&) {
								state->error.clear();
							}
						);

						if (!state->error.empty()) {
							ui::add_text(
								"mask preset save error",
								container,
								state->error,
								gfx::Color(255, 80, 80, 255),
								fonts::dejavu(fonts::size::SMALL)
							);
						}
					},
				.action_required = true,
				.close_on_confirm = false,
				.confirm_text = "Save",
				.on_confirm =
					[state, jpeg = std::move(jpeg)] {
						auto saved = save_mask_preset(jpeg, state->name);
						if (!saved) {
							state->error = saved.error();
							return;
						}

						ui::dialog::close();
						gui::components::notifications::add(
							std::format("Saved mask preset '{}'", u::path_to_string(saved->filename())),
							ui::NotificationType::SUCCESS
						);
					},
			}
		);
	}

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

	bool masking = (settings.interpolate || settings.deduplicate) && (!settings.mask.empty() || settings.auto_mask);
	if (!masking)
		show_mask_preview = false;

	bool showing_hovered_mask = !hovered_mask.empty() && hovered_mask != masks::NONE_OPTION;

	auto preview = preview_frames::update(
		{
			.video_path = preview_video_path,
			.settings = settings,
			.app_settings = app_settings,
			.seeking = dragging_seek_bar,
			.show_mask = show_mask_preview,
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

	const int mask_control_height = fonts::dejavu.height();

	auto add_mask_controls = [&] {
		if (!masking)
			return;

		container.push_element_gap(SEEK_BAR_BOTTOM_GAP);

		auto* separator = ui::add_separator("mask controls separator", container, ui::SeparatorStyle::FADE_BOTH);

		container.pop_element_gap();

		bool show_save_button =
			show_mask_preview && !showing_hovered_mask && preview.frame && preview.frame->up_to_date;

		if (show_save_button)
			container.push_element_gap(6);

		auto* show_mask_checkbox = ui::add_checkbox(
			"show mask preview checkbox", container, "show mask", show_mask_preview, fonts::dejavu, {}, true
		);

		if (show_save_button) {
			container.pop_element_gap();

			ui::set_next_same_line(container);

			auto* save_button = ui::add_icon_button(
				"save mask preset button",
				container,
				icons::SAVE,
				fonts::icons,
				gfx::Size(mask_control_height, mask_control_height),
				gfx::Color::white(190),
				gfx::Color::white(),
				[] {
					open_save_mask_dialog(preview_frames::current_mask_jpeg());
				},
				"Save mask as preset"
			);
		}
	};

	bool preview_image_added = false;
	if (showing_hovered_mask || preview.frame) {
		constexpr int preview_image_gap = 2;
		int seek_bar_height = ui::seek_bar_height(fonts::dejavu(fonts::size::SMALL));

		container.push_element_gap(preview_image_gap);

		if (showing_hovered_mask) {
			auto mask_path = masks::get_path() / u::string_to_path(hovered_mask);
			preview_image_added = ui::add_image(
									  "config preview image",
									  container,
									  mask_path,
									  container.get_usable_rect().size(),
									  "hovered mask " + hovered_mask,
									  gfx::Color::white()
			)
			                          .has_value();
		}
		else {
			// anything that isn't the finished blurred frame for the current settings is faded out
			preview_image_added = ui::add_image(
									  "config preview image",
									  container,
									  preview.frame->texture,
									  container.get_usable_rect().size(),
									  preview.frame->image_id,
									  gfx::Color::white(preview.frame->up_to_date ? 255 : 100)
			)
			                          .has_value();
		}

		container.pop_element_gap();

		container.push_element_gap(SEEK_BAR_BOTTOM_GAP);

		container.push_element_gap(DELETE_ICON_GAP);
		auto* seek_bar = ui::add_seek_bar(
			"config preview seek bar",
			container,
			app_settings.config_preview_seek,
			fonts::dejavu(fonts::size::SMALL),
			preview.video_duration,
			container.get_usable_rect().w - seek_bar_height - DELETE_ICON_GAP
		);

		seek_bar_dragging = ui::get_active_element() == seek_bar;

		// save once the drag is over rather than writing the config on every frame it moves
		if (app_settings.config_preview_seek != current_app_settings.config_preview_seek && !seek_bar_dragging) {
			save_preview_app_settings();
		}

		ui::set_next_same_line(container);
		container.pop_element_gap();

		ui::add_icon_button(
			"remove sample video button",
			container,
			icons::CLOSE,
			fonts::icons,
			gfx::Size(seek_bar_height, seek_bar_height),
			DELETE_ICON_COLOR,
			DELETE_ICON_HOVER_COLOR,
			[] {
				confirm_clear_sample_video();
			},
			"Remove sample video"
		);

		container.pop_element_gap();
	}
	else if (preview.rendering) {
		std::string loading_text = show_mask_preview ? "Loading mask preview..." : "Loading config preview...";
		if (preview.analysing_mask)
			loading_text = "Analysing video to generate a mask...";

		container.push_element_gap(SEEK_BAR_BOTTOM_GAP);

		ui::add_text(
			"loading config preview text",
			container,
			{ "", loading_text, "" }, // HACKY to get it to span more lines @todo: cleaner solution
			gfx::Color::white(100),
			fonts::dejavu,
			FONT_CENTERED_X
		);

		container.pop_element_gap();
	}
	else {
		container.push_element_gap(SEEK_BAR_BOTTOM_GAP);

		ui::add_text(
			"failed to generate preview text",
			container,
			{ "", "Failed to generate preview.", "" }, // HACKY to get it to span more lines @todo: cleaner solution
			gfx::Color::white(100),
			fonts::dejavu,
			FONT_CENTERED_X
		);

		container.pop_element_gap();
	}

	add_mask_controls();

	if (preview_image_added)
		ui::shrink_element_to_fit_container_height(container, "config preview image");
}

void configs::preview_tabs(ui::Container& header_container, ui::Container& content_container) {
	auto on_tab_select = [&content_container] {
		content_container.scroll_to_top = true;
		old_tab.clear();
	};

	ui::add_tabs("preview tab", header_container, TABS, selected_tab, fonts::dejavu, on_tab_select);
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

	preview_tabs(header_container, content_container);

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

	auto validation_res = config_blur::validate(settings, app_settings, encoding_preset_settings, false);
	std::string fixable_errors = validation_res.message(true);
	if (!fixable_errors.empty()) {
		ui::add_separator("config preview separator", content_container, ui::SeparatorStyle::FADE_BOTH);

		ui::add_button(
			"fix config button", content_container, "Reset invalid config options to defaults", fonts::dejavu, [] {
				config_blur::validate(settings, app_settings, encoding_preset_settings, true);
			}
		);
	}
}
