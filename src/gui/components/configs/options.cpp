#include "configs.h"
#include "../../fonts/icons.h"

#include "../../ui/ui.h"
#include "../../render/render.h"
#include "../../renderer.h"

#include "common/config_encoding_presets.h"
#include "common/config_app.h"
#include "common/masks.h"
#include "common/devices.h"
#include "common/interpolation_models.h"
#include "common/encoding.h"
#include "common/rendering/render_optimise.h"

namespace configs = gui::components::configs;

namespace {
	// the models aren't shipped with every install, so it's only offered once there's one
	bool gimm_installed() {
#ifdef TENSORRT
		return !interpolation_models::list_gimm_trt().empty();
#else
		return false;
#endif
	}

	// the pre-interpolation dropdown's copy of the method, with "none" for off
	std::string pre_interpolation_choice;

	// the tensorrt names are too long for two methods to fit on one line
	std::string short_method_name(const std::string& method) {
		if (method == "gimm-vfi (tensorrt)")
			return "gimm-vfi";
		if (method == "rife (tensorrt)")
			return "rife trt";
		return method;
	}
}

void configs::options(ui::Container& container) {
	hovered_weighting.clear();
	hovered_sampling.clear();
	hovered_mask.clear();

	auto validation = config_blur::validate(settings, app_settings, encoding_preset_settings, false);

	auto validated_element = [&](config_blur::ValidationField field,
	                             const std::string& error_id,
	                             const std::function<void()>& add_element) {
		auto it = std::ranges::find(validation.errors, field, &config_blur::ValidationError::field);
		std::optional<std::string> message = it != validation.errors.end() ? std::optional(it->message) : std::nullopt;
		ui::add_with_message(container, error_id, message, ERROR_COLOR, add_element);
	};

	bool first_section = true;
	auto section_component = [&](const std::string& label, bool* setting = nullptr, bool forced_on = false) {
		section(container, first_section, label, setting, forced_on);
	};

	auto model_dropdown = [&](const std::string& label,
	                          const std::vector<std::string>& models,
	                          std::string& model,
	                          const std::filesystem::path& folder) {
		auto options = models;
		std::vector<std::string> missing;
		if (!model.empty() && !u::contains(models, model)) {
			options.push_back(model);
			missing.push_back(model);
		}

		int button_size = ui::get_dropdown_box_height(fonts::dejavu);

		container.push_element_gap(DELETE_ICON_GAP);
		auto* dropdown = ui::add_dropdown(
			label + " dropdown",
			container,
			label,
			options,
			model,
			fonts::dejavu,
			{},
			missing,
			{},
			{},
			container.get_usable_rect().w - button_size - DELETE_ICON_GAP
		);
		container.pop_element_gap();

		ui::set_next_same_line(container);

		// align with the dropdown box specifically
		container.current_position.y = dropdown->element->rect.y2() - button_size;

		ui::add_icon_button(
			label + " folder button",
			container,
			icons::FOLDER,
			fonts::icons,
			gfx::Size(button_size, button_size),
			DELETE_ICON_COLOR,
			gfx::Color::white(),
			[folder] {
				std::string url = std::format("file://{}", u::path_to_string(folder));
				if (!SDL_OpenURL(url.c_str()))
					u::log_error("Failed to open models folder: {}", SDL_GetError());
			},
			"Open models folder"
		);
	};

	auto device_dropdown = [&](const std::string& label,
	                           const std::map<int, std::string>& device_list,
	                           std::string& device,
	                           const std::optional<std::string>& auto_device,
	                           std::string& selected) {
		if (devices::initialised && device_list.empty()) {
			selected = "none available";
			ui::add_dropdown(label + " dropdown", container, label, {}, selected, fonts::dejavu);
			return;
		}

		std::string auto_option = auto_device ? std::format("auto ({})", *auto_device) : "auto";

		std::vector<std::string> options = { auto_option };
		if (devices::initialised) {
			for (const auto& [index, name] : device_list)
				options.push_back(name);
		}

		std::vector<std::string> missing;
		if (device == "auto") {
			selected = auto_option;
		}
		else {
			selected = device;

			// add the current device as a muted option if its not available anymore
			if (devices::initialised && !u::contains(options, device)) {
				options.push_back(device);
				missing.push_back(device);
			}
		}

		ui::add_dropdown(
			label + " dropdown",
			container,
			label,
			options,
			selected,
			fonts::dejavu,
			[&device, auto_option](std::string* new_value) {
				device = *new_value == auto_option ? "auto" : *new_value;
			},
			missing
		);
	};

	/*
	    Blur
	*/
	section_component("blur", &settings.blur);

	if (settings.blur) {
		ui::add_slider_tied(
			"blur amount",
			container,
			0.f,
			2.f,
			&settings.blur_amount,
			"blur amount: {:.2f}",
			&settings.blur_output_fps,
			app_settings.blur_amount_tied_to_fps,
			"fps",
			fonts::dejavu,
			{},
			0.f,
			"",
			std::format("\xC2\xB7 {:.0f}\xC2\xB0", settings.blur_amount * 360)
		);

		ui::add_slider("output fps", container, 1, 120, &settings.blur_output_fps, "output fps: {} fps", fonts::dejavu);
		auto* weighting_dropdown = ui::add_dropdown(
			"blur weighting",
			container,
			"blur weighting",
			{
				"equal",
				"cosine",
				"gaussian_sym",
				"pyramid",
				"gaussian",
				"ascending",
				"descending",
				"gaussian_reverse",
			},
			settings.blur_weighting,
			fonts::dejavu
		);

		ui::AnimatedElement* sampling_dropdown = nullptr;
		validated_element(config_blur::ValidationField::BLUR_SAMPLING, "blur sampling error", [&] {
			sampling_dropdown = ui::add_dropdown(
				"blur sampling dropdown",
				container,
				"blur sampling",
				config_blur::BLUR_SAMPLING_METHODS,
				settings.blur_sampling,
				fonts::dejavu
			);
		});

		const auto& dropdown_data = std::get<ui::DropdownElementData>(weighting_dropdown->element->data);
		hovered_weighting = dropdown_data.hovered_option;

		set_temporary_tab(
			"blur weighting", weighting_dropdown->animations.at(ui::hasher("expand")).goal > 0, RIGHT_TABS[1]
		);

		if (sampling_dropdown) {
			hovered_sampling = std::get<ui::DropdownElementData>(sampling_dropdown->element->data).hovered_option;

			set_temporary_tab(
				"blur sampling", sampling_dropdown->animations.at(ui::hasher("expand")).goal > 0, RIGHT_TABS[1]
			);
		}

		ui::add_checkbox(
			"preserve brightness checkbox",
			container,
			"preserve brightness",
			settings.preserve_brightness,
			fonts::dejavu
		);

		ui::add_checkbox("bloom checkbox", container, "bloom", settings.bloom, fonts::dejavu);

		if (settings.bloom) {
			ui::add_slider(
				"bloom threshold",
				container,
				0.f,
				1.f,
				&settings.bloom_threshold,
				"bloom threshold: {:.2f}",
				fonts::dejavu,
				{},
				0.01f
			);

			ui::add_slider(
				"bloom strength",
				container,
				0.f,
				1.f,
				&settings.bloom_strength,
				"bloom strength: {:.2f}",
				fonts::dejavu,
				{},
				0.01f
			);
		}
	}

	/*
	    Interpolation
	*/
	section_component("interpolation", &settings.interpolate);

	if (settings.interpolate) {
		std::vector<std::string> interpolation_options = {
			"svp",
			"rife",
			"mvtools",
		};

		std::vector<std::string> pre_interpolation_options = {
			"rife",
		};

		if (devices::initialised && !devices::tensorrt.empty()) {
			interpolation_options.insert(interpolation_options.begin() + 2, "rife (tensorrt)");
			pre_interpolation_options.emplace_back("rife (tensorrt)");

			if (gimm_installed()) {
				interpolation_options.insert(interpolation_options.begin() + 3, "gimm-vfi (tensorrt)");
				pre_interpolation_options.emplace_back("gimm-vfi (tensorrt)");
			}
		}

		// pre-interpolating with the main method is skipped, so it isn't offered
		std::erase(pre_interpolation_options, settings.interpolation_method);

		bool pre_interpolating = settings.pre_interpolate && !pre_interpolation_options.empty();

		bool offer_pre_interpolation = !pre_interpolation_options.empty();

		if (offer_pre_interpolation) {
			pre_interpolation_choice = pre_interpolating ? settings.pre_interpolation_method : "none";

			auto pre_interpolation_choices = pre_interpolation_options;
			pre_interpolation_choices.emplace_back("none");

			ui::add_compact_dropdown(
				"pre-interpolation method dropdown",
				container,
				pre_interpolation_choices,
				pre_interpolation_choice,
				pre_interpolating ? short_method_name(settings.pre_interpolation_method) : "no pre-interp",
				pre_interpolating ? ui::PRE_INTERPOLATION_COLOR : gfx::Color(125, 125, 125, 255),
				fonts::dejavu,
				[](std::string* choice) {
					settings.pre_interpolate = *choice != "none";
					if (settings.pre_interpolate)
						settings.pre_interpolation_method = *choice;
				},
				{ "none" }
			);

			ui::set_next_same_line(container);
		}

		auto* method_dropdown = ui::add_compact_dropdown(
			"interpolation method dropdown",
			container,
			interpolation_options,
			settings.interpolation_method,
			short_method_name(settings.interpolation_method),
			gfx::Color(255, 100, 100, 255),
			fonts::dejavu,
			[pre_interpolation_options](std::string* method) {
				if (settings.pre_interpolation_method != *method)
					return;

				// it'd be skipped as the pre-interpolation method too, so that moves to another one
				auto other = std::ranges::find_if(pre_interpolation_options, [&](const std::string& option) {
					return option != *method;
				});
				if (other != pre_interpolation_options.end())
					settings.pre_interpolation_method = *other;
			}
		);

		// over the end of the track, where the main method's part is
		if (offer_pre_interpolation) {
			auto& method_rect = method_dropdown->element->rect;
			method_rect.x = container.get_usable_rect().x2() - method_rect.w;
			method_dropdown->element->orig_rect.x = method_rect.x;
		}

		ui::add_interpolation_track(
			"interpolation track",
			container,
			{
				.interpolated_fps = &settings.interpolated_fps,
				.pre_minimum_fps = &settings.pre_interpolated_minimum_fps,
				.pre_minimum_multiplier = &settings.pre_interpolated_minimum_multiplier,
				.show_pre = pre_interpolating,
				// otherwise the config isn't being edited for any particular video
				.source_fps = queue_video_preview ? preview_video_fps : std::nullopt,
				.font = fonts::dejavu,
			}
		);
	}

	/*
	    Retiming
	*/
	section_component("retiming");

	ui::add_checkbox("deduplicate checkbox", container, "deduplicate", settings.deduplicate, fonts::dejavu);

	ui::add_checkbox(
		"frame timing logs checkbox", container, "use frame timing logs", settings.frame_timing_logs, fonts::dejavu
	);

	if (settings.frame_timing_logs && settings.deduplicate) {
		ui::add_text(
			"deduplicate frame timing logs note",
			container,
			"deduplication is skipped for videos with a frame timing log",
			gfx::Color::white(renderer::MUTED_SHADE),
			fonts::dejavu
		);
	}

	if (settings.deduplicate || settings.frame_timing_logs) {
		// retiming happens as part of interpolation when it's on, so there's no method to pick
		if (settings.interpolate) {
			bool pre_interpolation_retimes =
				settings.pre_interpolate && settings.pre_interpolation_method != settings.interpolation_method;

			ui::add_text(
				"retime method interpolation note",
				container,
				pre_interpolation_retimes ? "retiming uses the pre-interpolation method"
										  : "retiming uses the interpolation method",
				gfx::Color::white(renderer::MUTED_SHADE),
				fonts::dejavu
			);
		}
		else {
			std::vector<std::string> retime_options = {
				"svp",
				"rife",
#ifdef TENSORRT
				"rife (tensorrt)",
#endif
				"mvtools",
				"old",
			};

			if (gimm_installed())
				retime_options.insert(retime_options.begin() + 3, "gimm-vfi (tensorrt)");

			ui::add_dropdown(
				"retime method dropdown",
				container,
				"retime method",
				retime_options,
				settings.retime_method,
				fonts::dejavu
			);
		}
	}

	/*
	    SVP
	*/
	if (settings.uses_interpolation_method("svp")) {
		section_component("svp");

		ui::add_checkbox(
			"auto svp gpu settings checkbox",
			container,
			"auto svp gpu settings",
			settings.optimise_gpu_settings,
			fonts::dejavu
		);

		if (settings.optimise_gpu_settings) {
			ui::add_checkbox(
				"auto svp prioritise speed checkbox",
				container,
				"prioritise speed",
				settings.optimise_prioritise_speed,
				fonts::dejavu
			);

			if (rendering::optimise::benchmarks(settings) &&
			    !rendering::optimise::list_saved(settings, app_settings).empty())
			{
				ui::add_button(
					"clear optimised settings button", container, "clear optimised settings", fonts::dejavu, [] {
						rendering::optimise::clear_saved(settings, app_settings);
					}
				);
			}
		}
		else {
			ui::add_checkbox(
				"svp gpu search checkbox", container, "svp gpu search", settings.svp_gpu_search, fonts::dejavu
			);

			if (devices::get_svp_gpu_supported() == false) {
				ui::add_text(
					"svp gpu rendering unsupported warning",
					container,
					"svp gpu rendering isn't supported by your gpu",
					WARNING_COLOR,
					fonts::dejavu
				);
			}
			else {
				ui::add_checkbox(
					"svp gpu rendering checkbox",
					container,
					"svp gpu rendering",
					settings.svp_gpu_rendering,
					fonts::dejavu
				);
			}
		}
	}

	/*
	    RIFE
	*/
	if (settings.uses_interpolation_method("rife")) {
		section_component("rife");

		// the folders are listed every frame so models dropped in show up straight away
		model_dropdown(
			"rife model", interpolation_models::list_rife(), settings.rife_model, interpolation_models::get_rife_path()
		);

		static std::string rife_device;
		device_dropdown(
			"rife device", devices::rife, app_settings.rife_device, devices::get_auto_rife_device(), rife_device
		);
	}

	/*
	    TensorRT
	*/
#ifdef TENSORRT
	bool uses_rife_trt = settings.uses_interpolation_method("rife (tensorrt)");
	bool uses_gimm = settings.uses_interpolation_method("gimm-vfi (tensorrt)");

	if (uses_rife_trt || uses_gimm) {
		section_component("tensorrt");

		if (uses_rife_trt) {
			model_dropdown(
				"rife (tensorrt) model",
				interpolation_models::list_rife_trt(),
				settings.rife_trt_model,
				interpolation_models::get_rife_trt_path()
			);

			ui::add_checkbox(
				"rife (tensorrt) ensemble checkbox",
				container,
				"rife (tensorrt) ensemble",
				settings.rife_trt_ensemble,
				fonts::dejavu
			);
		}

		if (uses_gimm) {
			model_dropdown(
				"gimm-vfi (tensorrt) model",
				interpolation_models::list_gimm_trt(),
				settings.gimm_trt_model,
				interpolation_models::get_gimm_trt_path()
			);
		}

		static std::string tensorrt_device;
		device_dropdown(
			"tensorrt device",
			devices::tensorrt,
			app_settings.tensorrt_device,
			devices::get_auto_tensorrt_device(),
			tensorrt_device
		);
	}
#endif

	/*
	    Scene detection
	*/
	if (settings.blur || settings.interpolate || settings.deduplicate)
		section_component("scene detection", &settings.scene_detection);

	/*
	    Masking
	*/
	if (config_blur::masking_possible(settings)) {
		section_component("masking");

		static std::string selected_mask;
		selected_mask = settings.mask.empty() ? masks::NONE_OPTION : settings.mask;

		auto* mask_dropdown = ui::add_dropdown(
			"default mask dropdown",
			container,
			"default mask",
			masks::options(settings.mask),
			selected_mask,
			fonts::dejavu,
			[](std::string* new_value) {
				configs::settings.mask = *new_value == masks::NONE_OPTION ? "" : *new_value;
			},
			{ masks::NONE_OPTION }
		);

		const auto& dropdown_data = std::get<ui::DropdownElementData>(mask_dropdown->element->data);
		hovered_mask = dropdown_data.hovered_option;

		ui::add_checkbox(
			"default auto mask checkbox", container, "default auto mask", settings.auto_mask, fonts::dejavu
		);

		if (settings.auto_mask) {
			ui::add_multiselect_dropdown(
				"auto mask extras dropdown",
				container,
				"auto mask extras",
				{
					{ "tracking", &settings.auto_mask_tracking },
					{ "layer", &settings.auto_mask_layer },
					{ "peel", &settings.auto_mask_peel },
					{ "inpainting", &settings.auto_mask_inpainting },
				},
				fonts::dejavu
			);
		}

		if (settings.uses_interpolation_method("svp"))
			ui::add_checkbox("svp hud mask checkbox", container, "svp hud mask", settings.svp_hud_mask, fonts::dejavu);
	}

	/*
	    Rendering
	*/
	section_component("rendering");

	auto presets = encoding::get_supported_encoding_presets(
		encoding_preset_settings, settings.gpu_encoding, app_settings.gpu_type
	);

	if (!presets.empty() && !u::contains(presets, settings.encode_preset)) {
		settings.encode_preset = presets[0];
	}

	if (presets.empty()) {
		ui::add_text(
			"no presets text",
			container,
			"no presets available. try toggling 'gpu encoding'",
			WARNING_COLOR,
			fonts::dejavu
		);
	}
	else {
		validated_element(config_blur::ValidationField::ENCODE_PRESET, "encode preset error", [&] {
			ui::add_dropdown(
				"codec dropdown",
				container,
				std::format("encode preset ({})", settings.gpu_encoding ? "gpu: " + app_settings.gpu_type : "cpu"),
				presets,
				settings.encode_preset,
				fonts::dejavu
			);
		});
	}

	if (settings.advanced.ffmpeg_override.empty()) {
		std::vector<std::string> preset_args = config_encoding_presets::get_preset_params(
			encoding_preset_settings,
			settings.gpu_encoding ? app_settings.gpu_type : "cpu",
			u::to_lower(settings.encode_preset.empty() ? "h264" : settings.encode_preset),
			settings.quality
		);

		auto codec = config_encoding_presets::extract_codec_from_args(preset_args);
		auto quality_config = config_encoding_presets::get_quality_config(codec ? *codec : "");

		// // clamp current quality to new range
		// settings.quality = std::clamp(settings.quality, quality_config.min_quality, quality_config.max_quality);

		ui::add_slider(
			"quality",
			container,
			quality_config.min_quality,
			quality_config.max_quality,
			&settings.quality,
			"quality: {}",
			fonts::dejavu,
			{},
			0.f,
			quality_config.quality_label
		);
	}
	else {
		ui::add_text(
			"ffmpeg override quality warning",
			container,
			"quality overridden by custom ffmpeg filters",
			WARNING_COLOR,
			fonts::dejavu
		);
	}

	ui::add_checkbox("preview checkbox", container, "preview", settings.preview, fonts::dejavu);

	ui::add_checkbox(
		"detailed filenames checkbox", container, "detailed filenames", settings.detailed_filenames, fonts::dejavu
	);

	ui::add_checkbox("copy dates checkbox", container, "copy dates", settings.copy_dates, fonts::dejavu);

	ui::add_text_input("output path input", container, app_settings.output_prefix, "output path", fonts::dejavu);

	ui::add_checkbox("upscale checkbox", container, "upscale", settings.upscale, fonts::dejavu);

	/*
	    GPU Acceleration
	*/
	section_component("gpu acceleration");

	ui::add_checkbox("gpu decoding checkbox", container, "gpu decoding", settings.gpu_decoding, fonts::dejavu);

	if (settings.advanced.ffmpeg_override.empty()) {
		ui::add_checkbox("gpu encoding checkbox", container, "gpu encoding", settings.gpu_encoding, fonts::dejavu);

		if (settings.gpu_encoding) {
			auto gpu_types = encoding::get_available_gpu_types();
			if (gpu_types.size() > 1) {
				ui::add_dropdown(
					"gpu encoding type dropdown",
					container,
					"gpu encoding device",
					gpu_types,
					app_settings.gpu_type,
					fonts::dejavu
				);
			}
		}
	}
	else {
		ui::add_text(
			"ffmpeg override gpu encoding warning",
			container,
			"gpu encoding overridden by custom ffmpeg filters",
			WARNING_COLOR,
			fonts::dejavu
		);
	}

	/*
	    Timescale
	*/
	section_component("timescale", &settings.timescale);

	if (settings.timescale) {
		ui::add_slider(
			"input timescale",
			container,
			0.f,
			2.f,
			&settings.input_timescale,
			"input timescale: {:.2f}",
			fonts::dejavu,
			{},
			0.01f
		);

		ui::add_slider(
			"output timescale",
			container,
			0.f,
			2.f,
			&settings.output_timescale,
			"output timescale: {:.2f}",
			fonts::dejavu,
			{},
			0.01f
		);

		ui::add_checkbox(
			"adjust timescaled audio pitch checkbox",
			container,
			"adjust timescaled audio pitch",
			settings.output_timescale_audio_pitch,
			fonts::dejavu
		);
	}

	/*
	    Filters
	*/
	section_component("filters", &settings.filters);

	if (settings.filters) {
		ui::add_slider(
			"brightness", container, 0.f, 2.f, &settings.brightness, "brightness: {:.2f}", fonts::dejavu, {}, 0.01f
		);
		ui::add_slider(
			"saturation", container, 0.f, 2.f, &settings.saturation, "saturation: {:.2f}", fonts::dejavu, {}, 0.01f
		);
		ui::add_slider(
			"contrast", container, 0.f, 2.f, &settings.contrast, "contrast: {:.2f}", fonts::dejavu, {}, 0.01f
		);
	}

	bool modified_advanced = settings.advanced != config_blur::DEFAULT_CONFIG.advanced;

	section_component("advanced", &settings.override_advanced, modified_advanced);

	if (settings.override_advanced) {
		/*
		    Advanced Deduplication
		*/
		section_component("advanced deduplication");

		if (settings.retime_method != "old") {
			ui::add_slider(
				"deduplicate range",
				container,
				-1,
				10,
				&settings.advanced.deduplicate_range,
				"deduplicate range: {}",
				fonts::dejavu,
				{},
				0.f,
				"-1 = infinite"
			);
		}

		validated_element(config_blur::ValidationField::DEDUPLICATE_THRESHOLD, "deduplicate threshold error", [&] {
			ui::add_text_input(
				"deduplicate threshold input",
				container,
				settings.advanced.deduplicate_threshold,
				"deduplicate threshold",
				fonts::dejavu
			);
		});

		ui::add_dropdown(
			"deduplicate real frame dropdown",
			container,
			"deduplicate real frame",
			{ "first", "last", "center", "surrounding" },
			settings.advanced.duplicate_timing,
			fonts::dejavu
		);

		if (settings.advanced.duplicate_timing == "surrounding") {
			ui::add_slider(
				"max future checks slider",
				container,
				0,
				10,
				&settings.advanced.max_future_checks,
				"max future checks: {}",
				fonts::dejavu
			);
		}

		/*
		    Advanced Rendering
		*/
		section_component("advanced rendering");

		ui::add_text_input(
			"video container text input", container, settings.advanced.video_container, "video container", fonts::dejavu
		);

		validated_element(config_blur::ValidationField::FFMPEG_OVERRIDE, "custom ffmpeg filters error", [&] {
			ui::add_text_input(
				"custom ffmpeg filters text input",
				container,
				settings.advanced.ffmpeg_override,
				"custom ffmpeg filters",
				fonts::dejavu
			);
		});

		ui::add_checkbox("debug checkbox", container, "debug", settings.advanced.debug, fonts::dejavu);

		ui::add_dropdown(
			"source plugin dropdown",
			container,
			"source plugin",
			config_blur::SOURCE_PLUGINS,
			settings.advanced.source_plugin,
			fonts::dejavu
		);

		/*
		    Advanced SVP
		*/
		bool uses_svp = settings.uses_interpolation_method("svp");
		bool uses_mvtools = settings.uses_interpolation_method("mvtools");

		// block size and mask area are shared with mvtools, so they still show when that's used on its own
		if (uses_svp)
			section_component("advanced svp");
		else if (uses_mvtools)
			section_component("advanced mvtools");

		if (uses_svp) {
			validated_element(
				config_blur::ValidationField::SVP_INTERPOLATION_PRESET, "SVP interpolation preset error", [&] {
					ui::add_dropdown(
						"SVP interpolation preset dropdown",
						container,
						"SVP interpolation preset",
						config_blur::SVP_INTERPOLATION_PRESETS,
						settings.advanced.svp_interpolation_preset,
						fonts::dejavu
					);
				}
			);

			validated_element(
				config_blur::ValidationField::SVP_INTERPOLATION_ALGORITHM, "SVP interpolation algorithm error", [&] {
					ui::add_dropdown(
						"SVP interpolation algorithm dropdown",
						container,
						"SVP interpolation algorithm",
						config_blur::SVP_INTERPOLATION_ALGORITHMS,
						settings.advanced.svp_interpolation_algorithm,
						fonts::dejavu
					);
				}
			);

			validated_element(config_blur::ValidationField::SVP_SEARCH_SIZE, "SVP search size error", [&] {
				ui::add_dropdown(
					"SVP search size dropdown",
					container,
					"SVP search size",
					config_blur::SVP_SEARCH_SIZES,
					settings.advanced.svp_search_size,
					fonts::dejavu
				);
			});

			validated_element(config_blur::ValidationField::SVP_BLOCK_OVERLAP, "SVP block overlap error", [&] {
				ui::add_dropdown(
					"SVP block overlap dropdown",
					container,
					"SVP block overlap",
					config_blur::SVP_BLOCK_OVERLAPS,
					settings.advanced.svp_block_overlap,
					fonts::dejavu
				);
			});
		}

		if (uses_svp || uses_mvtools) {
			std::string prefix = uses_svp ? "SVP " : "";

			validated_element(config_blur::ValidationField::SVP_BLOCK_SIZE, "SVP block size error", [&] {
				ui::add_dropdown(
					"svp block size dropdown",
					container,
					prefix + "block size",
					config_blur::SVP_BLOCK_SIZES,
					settings.advanced.svp_block_size,
					fonts::dejavu
				);
			});

			ui::add_slider(
				"svp mask area slider",
				container,
				0,
				500,
				&settings.advanced.svp_mask_area,
				prefix + "mask area: {}",
				fonts::dejavu
			);
		}

		/*
		    Advanced Masking
		*/
		if (config_blur::masking_possible(settings) && settings.auto_mask) {
			section_component("advanced masking");

			ui::add_slider(
				"auto mask stillness slider",
				container,
				0.f,
				1.f,
				&settings.advanced.auto_mask.stillness,
				"auto mask stillness: {:.2f}",
				fonts::dejavu,
				{},
				0.01f
			);

			ui::add_slider(
				"auto mask fill slider",
				container,
				0,
				100,
				&settings.advanced.auto_mask.fill,
				"auto mask fill: {}px",
				fonts::dejavu
			);

			ui::add_slider(
				"auto mask padding slider",
				container,
				0,
				32,
				&settings.advanced.auto_mask.padding,
				"auto mask padding: {}px",
				fonts::dejavu
			);

			ui::add_slider(
				"auto mask feather slider",
				container,
				0,
				32,
				&settings.advanced.auto_mask.feather,
				"auto mask feather: {}px",
				fonts::dejavu
			);

			ui::add_checkbox(
				"auto mask enclosed checkbox",
				container,
				"auto mask enclosed",
				settings.advanced.auto_mask.enclosed,
				fonts::dejavu
			);

			if (settings.advanced.auto_mask.enclosed) {
				ui::add_slider(
					"auto mask enclosed max fps slider",
					container,
					1,
					1000,
					&settings.advanced.auto_mask.enclosed_max_fps,
					"auto mask enclosed max fps: {}",
					fonts::dejavu
				);
			}

			ui::add_slider(
				"auto mask samples slider",
				container,
				4,
				64,
				&settings.advanced.auto_mask.samples,
				"auto mask samples: {}",
				fonts::dejavu
			);
		}

		/*
		    Advanced Blur
		*/
		section_component("advanced blur");

		ui::add_slider(
			"blur weighting gaussian std dev slider",
			container,
			0.f,
			10.f,
			&settings.advanced.blur_weighting_gaussian_std_dev,
			"blur weighting gaussian std dev: {:.2f}",
			fonts::dejavu
		);
		ui::add_slider(
			"blur weighting gaussian mean slider",
			container,
			0.f,
			2.f,
			&settings.advanced.blur_weighting_gaussian_mean,
			"blur weighting gaussian mean: {:.2f}",
			fonts::dejavu
		);
		ui::add_text_input(
			"blur weighting gaussian bound input",
			container,
			settings.advanced.blur_weighting_gaussian_bound,
			"blur weighting gaussian bound",
			fonts::dejavu
		);
	}
	else {
		// make sure theres no funny business (TODO: is this needed, are there edge cases?)
		settings.advanced = config_blur::DEFAULT_CONFIG.advanced;
	}
}

void configs::save_config() {
	flush_selected_config();

	// remove first so a config that was deleted and re-added under the same name is kept
	for (const auto& [name, config] : saved_configs) {
		if (!edited_configs.contains(name))
			config_blur::remove(name);
	}

	for (const auto& [name, config] : edited_configs) {
		auto existing = saved_configs.find(name);
		if (existing != saved_configs.end() && existing->second == config)
			continue;

		config_blur::save(name, config);
	}

	saved_configs = edited_configs;

	config_app::create(config_app::get_app_config_path(), app_settings);
	current_app_settings = app_settings;

	// the preset file is parsed back trimmed, so trim now to keep what's shown the same as what's saved
	for (auto& gpu_presets : encoding_preset_settings.all_gpu_presets) {
		for (auto& preset : gpu_presets.presets) {
			if (preset.is_default)
				continue;

			preset.name = u::trim(preset.name);
			preset.args = u::trim(preset.args);
		}
	}

	config_encoding_presets::save(encoding_preset_settings);
	current_encoding_preset_settings = encoding_preset_settings;

	// same for rule patterns
	for (auto& rule : rule_settings.rules) {
		rule.pattern = u::trim(rule.pattern);
	}

	config_rules::save(rule_settings);
	current_rule_settings = rule_settings;
};

void configs::on_load() {
	flush_selected_config();
	saved_configs = edited_configs;

	current_app_settings = app_settings;

	current_encoding_preset_settings = encoding_preset_settings;

	current_rule_settings = rule_settings;
};
