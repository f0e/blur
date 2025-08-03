#include "config_blur.h"
#include "config_base.h"

std::string config_blur::generate_config_string(const BlurSettings& settings, bool concise) {
	std::ostringstream output;

	output << "[blur v" << BLUR_VERSION << "]" << "\n";

	// Blur section
	if (!concise || settings.blur) {
		output << "\n";
		output << "- blur" << "\n";
		output << "blur: " << (settings.blur ? "true" : "false") << "\n";
		if (!concise || settings.blur) {
			output << "blur amount: " << settings.blur_amount << "\n";
			output << "blur output fps: " << settings.blur_output_fps << "\n";
			output << "blur weighting: " << settings.blur_weighting << "\n";
			output << "blur gamma: " << settings.blur_gamma << "\n";
		}
	}

	// Interpolation section
	if (!concise || settings.interpolate) {
		output << "\n";
		output << "- interpolation" << "\n";
		output << "interpolate: " << (settings.interpolate ? "true" : "false") << "\n";
		if (!concise || settings.interpolate) {
			output << "interpolated fps: " << settings.interpolated_fps << "\n";
			output << "interpolation method: " << settings.interpolation_method << "\n";
		}
	}

	// Pre-interpolation section
	if (!concise || settings.pre_interpolate) {
		output << "\n";
		output << "- pre-interpolation" << "\n";
		output << "pre-interpolate: " << (settings.pre_interpolate ? "true" : "false") << "\n";
		if (!concise || settings.pre_interpolate) {
			output << "pre-interpolated fps: " << settings.pre_interpolated_fps << "\n";
		}
	}

	// Deduplication section
	if (!concise || settings.deduplicate) {
		output << "\n";
		output << "- deduplication" << "\n";
		output << "deduplicate: " << (settings.deduplicate ? "true" : "false") << "\n";
		if (!concise || settings.deduplicate) {
			output << "deduplicate method: " << settings.deduplicate_method << "\n";
		}
	}

	// Rendering section (always included)
	output << "\n";
	output << "- rendering" << "\n";
	output << "encode preset: " << settings.encode_preset << "\n";
	output << "quality: " << settings.quality << "\n";
	if (!concise || settings.preview) {
		output << "preview: " << (settings.preview ? "true" : "false") << "\n";
	}
	if (!concise || settings.detailed_filenames) {
		output << "detailed filenames: " << (settings.detailed_filenames ? "true" : "false") << "\n";
	}
	if (!concise || settings.copy_dates) {
		output << "copy dates: " << (settings.copy_dates ? "true" : "false") << "\n";
	}

	// GPU acceleration section
	if (!concise || settings.gpu_decoding || settings.gpu_interpolation || settings.gpu_encoding) {
		output << "\n";
		output << "- gpu acceleration" << "\n";
		output << "gpu decoding: " << (settings.gpu_decoding ? "true" : "false") << "\n";
		output << "gpu interpolation: " << (settings.gpu_interpolation ? "true" : "false") << "\n";
		output << "gpu encoding: " << (settings.gpu_encoding ? "true" : "false") << "\n";
	}

	// Timescale section
	if (!concise || settings.timescale) {
		output << "\n";
		output << "- timescale" << "\n";
		output << "timescale: " << (settings.timescale ? "true" : "false") << "\n";
		if (!concise || settings.timescale) {
			output << "input timescale: " << settings.input_timescale << "\n";
			output << "output timescale: " << settings.output_timescale << "\n";
			if (!concise || settings.output_timescale_audio_pitch) {
				output << "adjust timescaled audio pitch: "
					   << (settings.output_timescale_audio_pitch ? "true" : "false") << "\n";
			}
		}
	}

	// Filters section
	if (!concise || settings.filters) {
		output << "\n";
		output << "- filters" << "\n";
		output << "filters: " << (settings.filters ? "true" : "false") << "\n";
		if (!concise || settings.filters) {
			output << "brightness: " << settings.brightness << "\n";
			output << "saturation: " << settings.saturation << "\n";
			output << "contrast: " << settings.contrast << "\n";
		}
	}

	// Advanced section
	if (!concise || settings.override_advanced) {
		output << "\n";
		output << "- advanced" << "\n";
		output << "advanced: " << (settings.override_advanced ? "true" : "false") << "\n";

		if (!concise || settings.override_advanced) {
			output << "\n";
			output << "- advanced deduplication" << "\n";
			output << "deduplicate range: " << settings.advanced.deduplicate_range << "\n";
			output << "deduplicate threshold: " << settings.advanced.deduplicate_threshold << "\n";

			output << "\n";
			output << "- advanced rendering" << "\n";
			output << "video container: " << settings.advanced.video_container << "\n";
			if (!concise || !settings.advanced.ffmpeg_override.empty()) {
				output << "custom ffmpeg filters: " << settings.advanced.ffmpeg_override << "\n";
			}
			if (!concise || settings.advanced.debug) {
				output << "debug: " << (settings.advanced.debug ? "true" : "false") << "\n";
			}

			output << "\n";
			output << "- advanced blur" << "\n";
			output << "blur weighting gaussian std dev: " << settings.advanced.blur_weighting_gaussian_std_dev << "\n";
			output << "blur weighting gaussian mean: " << settings.advanced.blur_weighting_gaussian_mean << "\n";
			output << "blur weighting gaussian bound: " << settings.advanced.blur_weighting_gaussian_bound << "\n";

			output << "\n";
			output << "- advanced interpolation" << "\n";
			output << "svp interpolation preset: " << settings.advanced.svp_interpolation_preset << "\n";
			output << "svp interpolation algorithm: " << settings.advanced.svp_interpolation_algorithm << "\n";
			output << "interpolation block size: " << settings.advanced.interpolation_blocksize << "\n";
			output << "interpolation mask area: " << settings.advanced.interpolation_mask_area << "\n";
			output << "rife model: " << settings.advanced.rife_model << "\n";

			if (!concise || settings.advanced.manual_svp) {
				output << "\n";
				output << "- manual svp override" << "\n";
				output << "manual svp: " << (settings.advanced.manual_svp ? "true" : "false") << "\n";
				if (!concise || settings.advanced.manual_svp) {
					output << "super string: " << settings.advanced.super_string << "\n";
					output << "vectors string: " << settings.advanced.vectors_string << "\n";
					output << "smooth string: " << settings.advanced.smooth_string << "\n";
				}
			}
		}
	}

	std::string result = output.str();

	// remove final newline if concise
	if (concise && !result.empty() && result.back() == '\n')
		result.pop_back();

	return result;
}

void config_blur::create(const std::filesystem::path& filepath, const BlurSettings& current_settings) {
	std::ofstream output(filepath);
	output << generate_config_string(current_settings, false);
}

std::string config_blur::export_concise(const BlurSettings& settings) {
	return generate_config_string(settings, true);
}

tl::expected<void, std::string> config_blur::validate(BlurSettings& config, bool fix) {
	std::set<std::string> errors;

	if (!u::contains(SVP_INTERPOLATION_PRESETS, config.advanced.svp_interpolation_preset)) {
		errors.insert(
			std::format("SVP interpolation preset ({}) is not a valid option", config.advanced.svp_interpolation_preset)
		);

		if (fix)
			config.advanced.svp_interpolation_preset = DEFAULT_CONFIG.advanced.svp_interpolation_preset;
	}

	if (!u::contains(SVP_INTERPOLATION_ALGORITHMS, config.advanced.svp_interpolation_algorithm)) {
		errors.insert(std::format(
			"SVP interpolation algorithm ({}) is not a valid option", config.advanced.svp_interpolation_algorithm
		));

		if (fix)
			config.advanced.svp_interpolation_algorithm = DEFAULT_CONFIG.advanced.svp_interpolation_algorithm;
	}

	if (!u::contains(INTERPOLATION_BLOCK_SIZES, config.advanced.interpolation_blocksize)) {
		errors.insert(
			std::format("Interpolation block size ({}) is not a valid option", config.advanced.interpolation_blocksize)
		);

		if (fix)
			config.advanced.interpolation_blocksize = DEFAULT_CONFIG.advanced.interpolation_blocksize;
	}

	if (!errors.empty())
		return tl::unexpected(u::join(errors, " "));

	return {};
}

BlurSettings config_blur::parse(const std::string& config_content) {
	std::istringstream stream(config_content);
	auto config_map = config_base::read_config_map(stream);
	return parse_from_map(config_map);
}

BlurSettings config_blur::parse(const std::filesystem::path& config_filepath) {
	std::ifstream file_stream(config_filepath);
	auto config_map = config_base::read_config_map(file_stream);
	return parse_from_map(config_map, config_filepath);
}

BlurSettings config_blur::parse_from_map(
	const std::map<std::string, std::string>& config_map, const std::optional<std::filesystem::path>& config_filepath
) {
	BlurSettings settings;

	config_base::extract_config_value(config_map, "blur", settings.blur);
	config_base::extract_config_value(config_map, "blur amount", settings.blur_amount);
	config_base::extract_config_value(config_map, "blur output fps", settings.blur_output_fps);
	config_base::extract_config_string(config_map, "blur weighting", settings.blur_weighting);
	config_base::extract_config_value(config_map, "blur gamma", settings.blur_gamma);

	config_base::extract_config_value(config_map, "interpolate", settings.interpolate);
	config_base::extract_config_string(config_map, "interpolated fps", settings.interpolated_fps);
	config_base::extract_config_string(config_map, "interpolation method", settings.interpolation_method);

	config_base::extract_config_value(config_map, "pre-interpolate", settings.pre_interpolate);
	config_base::extract_config_string(config_map, "pre-interpolated fps", settings.pre_interpolated_fps);

	config_base::extract_config_value(config_map, "deduplicate", settings.deduplicate);
	config_base::extract_config_value(config_map, "deduplicate method", settings.deduplicate_method);

	config_base::extract_config_value(config_map, "encode preset", settings.encode_preset);
	config_base::extract_config_value(config_map, "quality", settings.quality);
	config_base::extract_config_value(config_map, "preview", settings.preview);
	config_base::extract_config_value(config_map, "detailed filenames", settings.detailed_filenames);
	config_base::extract_config_value(config_map, "copy dates", settings.copy_dates);

	config_base::extract_config_value(config_map, "gpu decoding", settings.gpu_decoding);
	config_base::extract_config_value(config_map, "gpu interpolation", settings.gpu_interpolation);
	config_base::extract_config_value(config_map, "gpu encoding", settings.gpu_encoding);

	config_base::extract_config_value(config_map, "timescale", settings.timescale);
	config_base::extract_config_value(config_map, "input timescale", settings.input_timescale);
	config_base::extract_config_value(config_map, "output timescale", settings.output_timescale);
	config_base::extract_config_value(
		config_map, "adjust timescaled audio pitch", settings.output_timescale_audio_pitch
	);

	config_base::extract_config_value(config_map, "filters", settings.filters);
	config_base::extract_config_value(config_map, "brightness", settings.brightness);
	config_base::extract_config_value(config_map, "saturation", settings.saturation);
	config_base::extract_config_value(config_map, "contrast", settings.contrast);

	config_base::extract_config_value(config_map, "advanced", settings.override_advanced);

	if (settings.override_advanced) {
		config_base::extract_config_value(config_map, "deduplicate range", settings.advanced.deduplicate_range);
		config_base::extract_config_string(
			config_map, "deduplicate threshold", settings.advanced.deduplicate_threshold
		);

		config_base::extract_config_value(config_map, "video container", settings.advanced.video_container);
		config_base::extract_config_string(config_map, "custom ffmpeg filters", settings.advanced.ffmpeg_override);
		config_base::extract_config_value(config_map, "debug", settings.advanced.debug);

		config_base::extract_config_value(
			config_map, "blur weighting gaussian std dev", settings.advanced.blur_weighting_gaussian_std_dev
		);
		config_base::extract_config_value(
			config_map, "blur weighting gaussian mean", settings.advanced.blur_weighting_gaussian_mean
		);
		config_base::extract_config_string(
			config_map, "blur weighting gaussian bound", settings.advanced.blur_weighting_gaussian_bound
		);

		config_base::extract_config_string(
			config_map, "svp interpolation preset", settings.advanced.svp_interpolation_preset
		);
		config_base::extract_config_string(
			config_map, "svp interpolation algorithm", settings.advanced.svp_interpolation_algorithm
		);
		config_base::extract_config_string(
			config_map, "interpolation block size", settings.advanced.interpolation_blocksize
		);
		config_base::extract_config_value(
			config_map, "interpolation mask area", settings.advanced.interpolation_mask_area
		);
		config_base::extract_config_string(config_map, "rife model", settings.advanced.rife_model);
		config_base::extract_config_value(config_map, "manual svp", settings.advanced.manual_svp);
		config_base::extract_config_string(config_map, "super string", settings.advanced.super_string);
		config_base::extract_config_string(config_map, "vectors string", settings.advanced.vectors_string);
		config_base::extract_config_string(config_map, "smooth string", settings.advanced.smooth_string);
	}

	u::verify_gpu_encoding(settings);
	u::set_fastest_rife_gpu(settings);

	if (config_filepath) {
		// rewrite config with proper structure and default values
		create(*config_filepath, settings);
	}

	return settings;
}

BlurSettings config_blur::parse_global_config() {
	return parse(get_global_config_path());
}

std::filesystem::path config_blur::get_global_config_path() {
	return blur.settings_path / CONFIG_FILENAME;
}

std::filesystem::path config_blur::get_config_filename(const std::filesystem::path& video_folder) {
	return video_folder / CONFIG_FILENAME;
}

BlurSettings config_blur::get_global_config() {
	return config_base::load_config<BlurSettings>(get_global_config_path(), create, parse);
}

config_blur::ConfigRes config_blur::get_config(const std::filesystem::path& config_filepath, bool use_global) {
	bool local_cfg_exists = std::filesystem::exists(config_filepath);

	auto global_cfg_path = get_global_config_path();
	bool global_cfg_exists = std::filesystem::exists(global_cfg_path);

	ConfigRes res;
	std::filesystem::path cfg_path;

	if (use_global && !local_cfg_exists && global_cfg_exists) {
		cfg_path = global_cfg_path;

		if (blur.verbose)
			u::log("Using global config");
	}
	else {
		// check if the config file exists, if not, write the default values
		if (!local_cfg_exists) {
			create(config_filepath);

			u::log("Configuration file not found, default config generated at {}", config_filepath);
		}

		cfg_path = config_filepath;
	}

	res.config = parse(cfg_path);
	res.is_global = (cfg_path == global_cfg_path);

	return res;
}

tl::expected<nlohmann::json, std::string> BlurSettings::to_json() const {
	nlohmann::json j;

	j["blur"] = this->blur;
	j["blur_amount"] = this->blur_amount;
	j["blur_output_fps"] = this->blur_output_fps;
	j["blur_weighting"] = this->blur_weighting;
	j["blur_gamma"] = this->blur_gamma;

	j["interpolate"] = this->interpolate;
	j["interpolated_fps"] = this->interpolated_fps;
	j["interpolation_method"] = this->interpolation_method;

	j["pre_interpolate"] = this->pre_interpolate;
	j["pre_interpolated_fps"] = this->pre_interpolated_fps;

	j["deduplicate"] = this->deduplicate;
	j["deduplicate_method"] = this->deduplicate_method;

	j["timescale"] = this->timescale;
	j["input_timescale"] = this->input_timescale;
	j["output_timescale"] = this->output_timescale;
	j["output_timescale_audio_pitch"] = this->output_timescale_audio_pitch;

	j["filters"] = this->filters;
	j["brightness"] = this->brightness;
	j["saturation"] = this->saturation;
	j["contrast"] = this->contrast;

	j["encode preset"] = this->encode_preset;
	j["quality"] = this->quality;
	j["preview"] = this->preview;
	j["detailed_filenames"] = this->detailed_filenames;
	// j["copy_dates"] = this->copy_dates;

	j["gpu_decoding"] = this->gpu_decoding;
	j["gpu_interpolation"] = this->gpu_interpolation;
	j["gpu_encoding"] = this->gpu_encoding;

	j["filters"] = this->filters;
	j["brightness"] = this->brightness;
	j["saturation"] = this->saturation;
	j["contrast"] = this->contrast;

	// advanced
	j["deduplicate_range"] = this->advanced.deduplicate_range;
	j["deduplicate_threshold"] = this->advanced.deduplicate_threshold;

	// j["video_container"] = this->advanced.video_container;
	// j["ffmpeg_override"] = this->advanced.ffmpeg_override;
	j["debug"] = this->advanced.debug;

	j["blur_weighting_gaussian_std_dev"] = this->advanced.blur_weighting_gaussian_std_dev;
	j["blur_weighting_gaussian_mean"] = this->advanced.blur_weighting_gaussian_mean;
	j["blur_weighting_gaussian_bound"] = this->advanced.blur_weighting_gaussian_bound;

	j["svp_interpolation_preset"] = this->advanced.svp_interpolation_preset;
	j["svp_interpolation_algorithm"] = this->advanced.svp_interpolation_algorithm;
	j["interpolation_blocksize"] = this->advanced.interpolation_blocksize;
	j["interpolation_mask_area"] = this->advanced.interpolation_mask_area;

	auto rife_model_path = get_rife_model_path();
	if (!rife_model_path)
		return tl::unexpected(rife_model_path.error());

	j["rife_model"] = *rife_model_path;

	j["manual_svp"] = this->advanced.manual_svp;
	j["super_string"] = this->advanced.super_string;
	j["vectors_string"] = this->advanced.vectors_string;
	j["smooth_string"] = this->advanced.smooth_string;

	return j;
}

BlurSettings::BlurSettings() {
	u::verify_gpu_encoding(*this);
}

// NOLINTBEGIN(readability-convert-member-functions-to-static) other platforms need it
tl::expected<std::filesystem::path, std::string> BlurSettings::get_rife_model_path() const {
	// NOLINTEND(readability-convert-member-functions-to-static)
	std::filesystem::path rife_model_path;

#if defined(_WIN32)
	rife_model_path = u::get_resources_path() / "lib/models" / this->advanced.rife_model;
#elif defined(__linux__)
	rife_model_path = u::get_resources_path() / "models" / this->advanced.rife_model;
#elif defined(__APPLE__)
	rife_model_path = u::get_resources_path() / "models" / this->advanced.rife_model;
#endif

	if (!std::filesystem::exists(rife_model_path))
		return tl::unexpected(std::format("RIFE model '{}' could not be found", this->advanced.rife_model));

	return rife_model_path;
}
