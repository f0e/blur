#include "configs.h"
#include "../../renderer.h"

#include "../../ui/ui.h"
#include "../../render/render.h"

#include "common/config_presets.h"
#include "common/config_app.h"

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
	static const gfx::Color section_color = gfx::Color::white(renderer::MUTED_SHADE);

	bool first_section = true;
	auto section_component = [&](std::string label, bool* setting = nullptr, bool forced_on = false) {
		if (!first_section) {
			ui::add_separator(std::format("section {} separator", label), container, ui::SeparatorStyle::FADE_RIGHT);
		}
		else
			first_section = false;

		if (!setting)
			return;

		if (!forced_on) {
			ui::add_checkbox(std::format("section {} checkbox", label), container, label, *setting, fonts::dejavu);
		}
		else {
			ui::add_text(std::format("section {}", label), container, label, gfx::Color::white(), fonts::dejavu);

			ui::add_text(
				std::format("section {} forced", label),
				container,
				"forced on as settings in this section have been modified",
				gfx::Color::white(renderer::MUTED_SHADE),
				fonts::dejavu
			);
		}
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

		ui::add_dropdown(
			"interpolation method dropdown",
			container,
			"interpolation method",
			{
				"svp",
				"rife", // plugins broken on mac rn idk why todo: fix when its fixed
			},
			settings.interpolation_method,
			fonts::dejavu
		);
	}

	/*
	    Pre-interpolation
	*/
	if (settings.interpolate && settings.interpolation_method != "rife") {
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
		}
	}

	/*
	    Deduplication
	*/
	section_component("deduplication");

	ui::add_checkbox("deduplicate checkbox", container, "deduplicate", settings.deduplicate, fonts::dejavu);

	if (settings.deduplicate) {
		ui::add_dropdown(
			"deduplicate method dropdown",
			container,
			"deduplicate method",
			{
				"svp",
				"rife",
				"old",
			},
			settings.deduplicate_method,
			fonts::dejavu
		);
	}

	/*
	    Rendering
	*/
	section_component("rendering");

	ui::add_dropdown(
		"codec dropdown",
		container,
		std::format("encode preset ({})", settings.gpu_encoding ? "gpu: " + app_settings.gpu_type : "cpu"),
		u::get_supported_presets(settings.gpu_encoding, app_settings.gpu_type),
		settings.encode_preset,
		fonts::dejavu
	);

	if (settings.advanced.ffmpeg_override.empty()) {
		std::vector<std::string> preset_args = config_presets::get_preset_params(
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
			gfx::Color(252, 186, 3, 150),
			fonts::dejavu
		);
	}

	ui::add_checkbox("preview checkbox", container, "preview", settings.preview, fonts::dejavu);

	ui::add_checkbox(
		"detailed filenames checkbox", container, "detailed filenames", settings.detailed_filenames, fonts::dejavu
	);

	ui::add_checkbox("copy dates checkbox", container, "copy dates", settings.copy_dates, fonts::dejavu);

	ui::add_text_input("output path input", container, app_settings.output_prefix, "output path", fonts::dejavu);

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
			gfx::Color(252, 186, 3, 150),
			fonts::dejavu
		);
	}

	static std::string rife_gpu;

	if (app_settings.rife_gpu_index == -1) {
		rife_gpu = "default - will use first available";
	}
	else {
		if (blur.initialised_rife_gpus && !blur.rife_gpus.empty()) {
			rife_gpu = blur.rife_gpus.at(app_settings.rife_gpu_index);
		}
		else {
			rife_gpu = std::format("gpu {}", app_settings.rife_gpu_index);
		}
	}

	ui::add_dropdown(
		"rife gpu dropdown",
		container,
		"rife gpu",
		blur.rife_gpu_names,
		rife_gpu,
		fonts::dejavu,
		[&](std::string* new_gpu_name) {
			for (const auto& [gpu_index, gpu_name] : blur.rife_gpus) {
				if (gpu_name == *new_gpu_name) {
					app_settings.rife_gpu_index = gpu_index;
				}
			}
		}
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

		std::istringstream iss(settings.advanced.deduplicate_threshold);
		float f = NAN;
		iss >> std::noskipws >> f; // try to read as float
		bool is_float = iss.eof() && !iss.fail();

		if (!is_float)
			container.push_element_gap(2);

		ui::add_text_input(
			"deduplicate threshold input",
			container,
			settings.advanced.deduplicate_threshold,
			"deduplicate threshold",
			fonts::dejavu
		);

		if (!is_float) {
			container.pop_element_gap();

			ui::add_text(
				"deduplicate threshold not a float warning",
				container,
				"deduplicate threshold must be a decimal number",
				gfx::Color(255, 0, 0, 255),
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

		bool bad_audio = settings.timescale && (u::contains(settings.advanced.ffmpeg_override, "-c:a copy") ||
		                                        u::contains(settings.advanced.ffmpeg_override, "-codec:a copy"));
		if (bad_audio)
			container.push_element_gap(2);

		ui::add_text_input(
			"custom ffmpeg filters text input",
			container,
			settings.advanced.ffmpeg_override,
			"custom ffmpeg filters",
			fonts::dejavu
		);

		if (bad_audio) {
			container.pop_element_gap();

			ui::add_text(
				"timescale audio copy warning",
				container,
				"cannot use -c:a copy while using timescale",
				gfx::Color(255, 0, 0, 255),
				fonts::dejavu
			);
		}

		ui::add_checkbox("debug checkbox", container, "debug", settings.advanced.debug, fonts::dejavu);

		/*
		    Advanced Interpolation
		*/
		section_component("advanced interpolation");

		if (settings.interpolation_method == "svp") {
			ui::add_dropdown(
				"SVP interpolation preset dropdown",
				container,
				"SVP interpolation preset",
				config_blur::SVP_INTERPOLATION_PRESETS,
				settings.advanced.svp_interpolation_preset,
				fonts::dejavu
			);

			ui::add_dropdown(
				"SVP interpolation algorithm dropdown",
				container,
				"SVP interpolation algorithm",
				config_blur::SVP_INTERPOLATION_ALGORITHMS,
				settings.advanced.svp_interpolation_algorithm,
				fonts::dejavu
			);
		}

		ui::add_dropdown(
			"interpolation block size dropdown",
			container,
			"interpolation block size",
			config_blur::INTERPOLATION_BLOCK_SIZES,
			settings.advanced.interpolation_blocksize,
			fonts::dejavu
		);

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
};

void configs::on_load() {
	current_global_settings = settings;
	parse_interp();

	current_app_settings = app_settings;
};
