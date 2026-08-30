#include "configs.h"

#include "../../ui/ui.h"
#include "../../render/render.h"

#include "common/config_presets.h"
#include "common/config_app.h"
#include "common/masks.h"

namespace configs = gui::components::configs;

void configs::set_interpolated_fps() {
	if (interpolate_scale) {
		settings.interpolated_fps = std::format("{}x", interpolated_fps_mult);
	}
	else {
		settings.interpolated_fps = std::to_string(interpolated_fps);
	}

	if (pre_interpolate_scale) {
		if (interpolate_scale)
			pre_interpolated_fps_mult =
				std::min(pre_interpolated_fps_mult, interpolated_fps_mult); // can't preinterpolate more than interp

		settings.pre_interpolated_fps = std::format("{}x", pre_interpolated_fps_mult);
	}
	else {
		if (!interpolate_scale)
			pre_interpolated_fps =
				std::min(pre_interpolated_fps, interpolated_fps); // can't preinterpolate more than interp

		settings.pre_interpolated_fps = std::to_string(pre_interpolated_fps);
	}
}

void configs::options(ui::Container& container) {
	// try set fastest devices if it hasnt been set yet. this will run on config load, but devices may not have been
	// initialised by the time you switch to the config screen. this catches that case.
	u::set_fastest_devices(settings);

	auto validation = config_blur::validate(settings, app_settings, preset_settings, false);

	auto validated_element = [&](config_blur::ValidationField field,
	                             const std::string& error_id,
	                             const std::function<void()>& add_element) {
		auto it = std::ranges::find(validation.errors, field, &config_blur::ValidationError::field);
		std::optional<std::string> message = it != validation.errors.end() ? std::optional(it->message) : std::nullopt;
		add_with_message(container, error_id, message, ERROR_COLOR, add_element);
	};

	bool first_section = true;
	auto section_component = [&](const std::string& label, bool* setting = nullptr, bool forced_on = false) {
		section(container, first_section, label, setting, forced_on);
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
			fonts::dejavu
		);

		ui::add_slider("output fps", container, 1, 120, &settings.blur_output_fps, "output fps: {} fps", fonts::dejavu);
		auto* weighting_dropdown = ui::add_dropdown(
			"blur weighting",
			container,
			"blur weighting",
			{
				"equal",
				"gaussian_sym",
				"vegas",
				"pyramid",
				"gaussian",
				"ascending",
				"descending",
				"gaussian_reverse",
			},
			settings.blur_weighting,
			fonts::dejavu
		);

		const auto& dropdown_data = std::get<ui::DropdownElementData>(weighting_dropdown->element->data);
		hovered_weighting = dropdown_data.hovered_option;

		if (weighting_dropdown->animations.at(ui::hasher("expand")).goal > 0) {
			if (old_tab.empty()) {
				old_tab = selected_tab;
				selected_tab = "weightings";
			}
		}
		else {
			if (!old_tab.empty()) {
				selected_tab = old_tab;
				old_tab.clear();
			}
		}

		ui::add_slider("blur gamma", container, 1.f, 10.f, &settings.blur_gamma, "blur gamma: {:.2f}", fonts::dejavu);
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
			fonts::dejavu,
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
				fonts::dejavu,
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
				settings.blur_output_fps * 50,
				&interpolated_fps,
				"interpolated fps: {} fps",
				fonts::dejavu,
				[&](std::variant<int*, float*> value) {
					set_interpolated_fps();
				}
			);
		}

		std::vector<std::string> interpolation_options = {
			"svp",
			"rife",
			"mvtools",
		};

		if (blur.initialised_devices && !blur.tensorrt_devices.empty()) {
			interpolation_options.insert(interpolation_options.begin() + 2, "rife (tensorrt)");
		}

		ui::add_dropdown(
			"interpolation method dropdown",
			container,
			"interpolation method",
			interpolation_options,
			settings.interpolation_method,
			fonts::dejavu
		);
	}

	/*
	    Pre-interpolation
	*/
	if (settings.interpolate) {
		section_component("pre-interpolation", &settings.pre_interpolate);

		if (settings.pre_interpolate) {
			ui::add_checkbox(
				"pre-interpolate scale checkbox",
				container,
				"pre-interpolate by scaling fps",
				pre_interpolate_scale,
				fonts::dejavu,
				[&](bool new_value) {
					set_interpolated_fps();
				}
			);

			if (pre_interpolate_scale) {
				ui::add_slider(
					"pre-interpolated fps mult",
					container,
					1.f,
					interpolate_scale ? interpolated_fps_mult : 10.f,
					&pre_interpolated_fps_mult,
					"pre-interpolated fps: {:.1f}x",
					fonts::dejavu,
					[&](std::variant<int*, float*> value) {
						set_interpolated_fps();
					},
					0.1f
				);
			}
			else {
				ui::add_slider(
					"pre-interpolated fps",
					container,
					1,
					!interpolate_scale ? interpolated_fps : 2400,
					&pre_interpolated_fps,
					"pre-interpolated fps: {} fps",
					fonts::dejavu,
					[&](std::variant<int*, float*> value) {
						set_interpolated_fps();
					}
				);
			}

			std::vector<std::string> pre_interpolation_options = {
				"rife",
			};

			if (blur.initialised_devices && !blur.tensorrt_devices.empty()) {
				pre_interpolation_options.insert(pre_interpolation_options.end(), "rife (tensorrt)");
			}

			ui::add_dropdown(
				"pre-interpolation method dropdown",
				container,
				"pre-interpolation method",
				pre_interpolation_options,
				settings.pre_interpolation_method,
				fonts::dejavu
			);
		}
	}

	/*
	    Deduplication
	*/
	section_component("deduplication");

	ui::add_checkbox("deduplicate checkbox", container, "deduplicate", settings.deduplicate, fonts::dejavu);

	if (settings.deduplicate) {
		// deduplication generates its frames as part of the interpolation pass, so when that's running there's
		// no second method to pick - see blur/deduplicate.py
		if (settings.interpolate) {
			ui::add_text(
				"deduplicate method interpolation note",
				container,
				"filled by the interpolation method",
				WARNING_COLOR,
				fonts::dejavu
			);
		}
		else {
			ui::add_dropdown(
				"deduplicate method dropdown",
				container,
				"deduplicate method",
				{
					"svp",
					"rife",
#ifdef TENSORRT
					"rife (tensorrt)",
#endif
					"mvtools",
					"old",
				},
				settings.deduplicate_method,
				fonts::dejavu
			);
		}
	}

	/*
	    Masking
	*/
	// interpolation and deduplication are both things a mask protects against, so this sits after the pair of
	// them rather than under either one. no point offering it when neither is going to run
	if (settings.interpolate || settings.deduplicate) {
		section_component("masking");

		// the dropdown holds onto a pointer to this, so it has to outlive the frame
		static std::string selected_mask;
		selected_mask = settings.mask.empty() ? masks::NONE_OPTION : settings.mask;

		ui::add_dropdown(
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
	}

	/*
	    Rendering
	*/
	section_component("rendering");

	auto presets = u::get_supported_presets(preset_settings, settings.gpu_encoding, app_settings.gpu_type);

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
		std::vector<std::string> preset_args = config_presets::get_preset_params(
			preset_settings,
			settings.gpu_encoding ? app_settings.gpu_type : "cpu",
			u::to_lower(settings.encode_preset.empty() ? "h264" : settings.encode_preset),
			settings.quality
		);

		auto codec = config_presets::extract_codec_from_args(preset_args);
		auto quality_config = config_presets::get_quality_config(codec ? *codec : "");

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

	ui::add_checkbox(
		"gpu interpolation checkbox", container, "gpu interpolation", settings.gpu_interpolation, fonts::dejavu
	);

	if (settings.advanced.ffmpeg_override.empty()) {
		ui::add_checkbox("gpu encoding checkbox", container, "gpu encoding", settings.gpu_encoding, fonts::dejavu);

		if (settings.gpu_encoding) {
			auto gpu_types = u::get_available_gpu_types();
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

	static std::string rife_device;

	if (app_settings.rife_device_index == -1) {
		rife_device = "default - will use first available";
	}
	else {
		if (blur.initialised_devices && !blur.rife_devices.empty()) {
			rife_device = blur.rife_devices.at(app_settings.rife_device_index);
		}
		else {
			rife_device = std::format("gpu {}", app_settings.rife_device_index);
		}
	}

	ui::add_dropdown(
		"rife device dropdown",
		container,
		"rife device",
		blur.rife_device_names,
		rife_device,
		fonts::dejavu,
		[&](std::string* new_gpu_name) {
			for (const auto& [device_index, gpu_name] : blur.rife_devices) {
				if (gpu_name == *new_gpu_name) {
					app_settings.rife_device_index = device_index;
				}
			}
		}
	);

#ifdef TENSORRT
	static std::string tensorrt_device;

	if (app_settings.tensorrt_device_index == -1) {
		if (blur.initialised_devices) {
			tensorrt_device = "no tensorrt devices available";
		}
		else {
			tensorrt_device = "default - will use first available";
		}
	}
	else {
		if (blur.initialised_devices && !blur.tensorrt_devices.empty()) {
			tensorrt_device = blur.tensorrt_devices.at(app_settings.tensorrt_device_index);
		}
		else {
			tensorrt_device = std::format("gpu {}", app_settings.tensorrt_device_index);
		}
	}

	ui::add_dropdown(
		"tensorrt device dropdown",
		container,
		"rife (tensorrt) device",
		blur.tensorrt_device_names,
		tensorrt_device,
		fonts::dejavu,
		[&](std::string* new_gpu_name) {
			for (const auto& [device_index, gpu_name] : blur.tensorrt_devices) {
				if (gpu_name == *new_gpu_name) {
					app_settings.tensorrt_device_index = device_index;
				}
			}
		}
	);
#endif

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

		if (settings.deduplicate_method != "old") {
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
			{ "first", "last" },
			settings.advanced.duplicate_timing,
			fonts::dejavu
		);

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
			"resize chroma location dropdown",
			container,
			"resize chroma location",
			config_blur::RESIZE_CHROMA_LOCATIONS,
			settings.advanced.resize_chromaloc,
			fonts::dejavu
		);

		ui::add_dropdown(
			"source plugin dropdown",
			container,
			"source plugin",
			config_blur::SOURCE_PLUGINS,
			settings.advanced.source_plugin,
			fonts::dejavu
		);

		/*
		    Advanced Interpolation
		*/
		section_component("advanced interpolation");

		if (settings.interpolation_method == "svp") {
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
		}

		validated_element(config_blur::ValidationField::INTERPOLATION_BLOCKSIZE, "interpolation block size error", [&] {
			ui::add_dropdown(
				"interpolation block size dropdown",
				container,
				"interpolation block size",
				config_blur::INTERPOLATION_BLOCK_SIZES,
				settings.advanced.interpolation_blocksize,
				fonts::dejavu
			);
		});

		ui::add_slider(
			"interpolation mask area slider",
			container,
			0,
			500,
			&settings.advanced.interpolation_mask_area,
			"interpolation mask area: {}",
			fonts::dejavu
		);

		ui::add_text_input("rife model", container, settings.advanced.rife_model, "rife model", fonts::dejavu);

#ifdef TENSORRT
		ui::add_text_input(
			"rife (tensorrt) model", container, settings.advanced.rife_trt_model, "rife (tensorrt) model", fonts::dejavu
		);
#endif

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

void configs::parse_interp() {
	auto parse_fps_setting = [&](const std::string& fps_setting,
	                             int& fps,
	                             float& fps_mult,
	                             bool& scale_mode,
	                             const std::function<void()>& set_function,
	                             const std::string& log_prefix = "") {
		try {
			auto split = u::split_string(fps_setting, "x");
			if (split.size() > 1) {
				fps_mult = std::stof(split[0]);
				scale_mode = true;
			}
			else {
				fps = std::stof(fps_setting);
				scale_mode = false;
			}

			u::log("loaded {}interp, scale: {} (fps: {}, mult: {})", log_prefix, scale_mode, fps, fps_mult);
		}
		catch (std::exception& e) {
			u::log("failed to parse {}interpolated fps, setting defaults cos user error", log_prefix);
			set_function();
		}
	};

	parse_fps_setting(
		settings.interpolated_fps, interpolated_fps, interpolated_fps_mult, interpolate_scale, set_interpolated_fps
	);

	parse_fps_setting(
		settings.pre_interpolated_fps,
		pre_interpolated_fps,
		pre_interpolated_fps_mult,
		pre_interpolate_scale,
		set_interpolated_fps,
		"pre-"
	);
};

void configs::save_config() {
	config_blur::create(config_blur::get_global_config_path(), settings);
	current_global_settings = settings;

	config_app::create(config_app::get_app_config_path(), app_settings);
	current_app_settings = app_settings;

	// the preset file is parsed back trimmed, so trim now to keep what's shown the same as what's saved
	for (auto& gpu_presets : preset_settings.all_gpu_presets) {
		for (auto& preset : gpu_presets.presets) {
			if (preset.is_default)
				continue;

			preset.name = u::trim(preset.name);
			preset.args = u::trim(preset.args);
		}
	}

	config_presets::save(preset_settings);
	current_preset_settings = preset_settings;
};

void configs::on_load() {
	current_global_settings = settings;
	parse_interp();

	current_app_settings = app_settings;

	current_preset_settings = preset_settings;
};
