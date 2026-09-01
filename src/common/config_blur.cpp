#include "config_blur.h"
#include "masks.h"
#include "config_base.h"
#include "config_app.h"
#include "rendering/render_commands.h"

namespace {
	bool deduplicate_threshold_valid(const std::string& threshold) {
		std::istringstream iss(threshold);
		float f = NAN;
		iss >> std::noskipws >> f; // try to read as float

		return iss.eof() && !iss.fail();
	}
}

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
			output << "pre-interpolation method: " << settings.pre_interpolation_method << "\n";
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

	// Masking section - after the two things it protects against, since it applies to both
	if (!concise || !settings.mask.empty() || settings.auto_mask) {
		output << "\n";
		output << "- masking" << "\n";
		output << "mask: " << settings.mask << "\n";
		output << "auto mask: " << (settings.auto_mask ? "true" : "false") << "\n";
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
	if (!concise || settings.upscale) {
		output << "upscale: " << (settings.upscale ? "true" : "false") << "\n";
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
			output << "deduplicate real frame: " << settings.advanced.duplicate_timing << "\n";
			output << "deduplicate max future checks: " << settings.advanced.max_future_checks << "\n";

			output << "\n";
			output << "- advanced rendering" << "\n";
			output << "video container: " << settings.advanced.video_container << "\n";
			if (!concise || !settings.advanced.ffmpeg_override.empty()) {
				output << "custom ffmpeg filters: " << settings.advanced.ffmpeg_override << "\n";
			}
			if (!concise || settings.advanced.debug) {
				output << "debug: " << (settings.advanced.debug ? "true" : "false") << "\n";
			}
			output << "resizing chroma location: " << settings.advanced.resize_chromaloc << "\n";
			output << "source plugin: " << settings.advanced.source_plugin << "\n";

			output << "\n";
			output << "- advanced masking" << "\n";
			output << "auto mask samples: " << settings.advanced.auto_mask.samples << "\n";
			output << "auto mask stillness: " << settings.advanced.auto_mask.stillness << "\n";
			output << "auto mask fill: " << settings.advanced.auto_mask.fill << "\n";
			output << "auto mask padding: " << settings.advanced.auto_mask.padding << "\n";
			output << "auto mask feather: " << settings.advanced.auto_mask.feather << "\n";

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
#ifdef TENSORRT
			output << "rife (tensorrt) model: " << settings.advanced.rife_trt_model << "\n";
#endif

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
	config_base::write_config_string(filepath, generate_config_string(current_settings, false));
}

bool config_blur::same_masking(const BlurSettings& a, const BlurSettings& b) {
	if (a.mask != b.mask || a.auto_mask != b.auto_mask || a.advanced.auto_mask != b.advanced.auto_mask)
		return false;

	if (a.interpolate != b.interpolate || a.deduplicate != b.deduplicate ||
	    a.advanced.deduplicate_range != b.advanced.deduplicate_range)
		return false;

	return a.advanced.source_plugin == b.advanced.source_plugin && a.gpu_decoding == b.gpu_decoding;
}

std::string config_blur::export_concise(const BlurSettings& settings) {
	return generate_config_string(settings, true);
}

std::string config_blur::ValidationResult::message(bool fixable_only) const {
	std::vector<std::string> messages;

	for (const auto& error : errors) {
		if (fixable_only && !error.fixable)
			continue;

		messages.push_back(error.message);
	}

	return u::join(messages, " ");
}

config_blur::ValidationResult config_blur::validate(
	BlurSettings& config, const GlobalAppSettings& app_settings, const EncodingPresetSettings& presets, bool fix
) {
	ValidationResult result;

	auto add_error = [&](ValidationField field, std::string message, bool fixable = false) {
		result.errors.emplace_back(field, std::move(message), fixable);
	};

	bool timescaling = config.timescale && config.output_timescale != config.input_timescale;
	if (timescaling && rendering::detail::copies_audio(config, app_settings, presets)) {
		if (!config.advanced.ffmpeg_override.empty()) {
			add_error(ValidationField::FFMPEG_OVERRIDE, "cannot use -c:a copy while using timescale");
		}
		else {
			add_error(
				ValidationField::ENCODE_PRESET, "this preset uses -c:a copy, which can't be used while using timescale"
			);
		}
	}

	if (config.override_advanced) {
		// only check advanced settings when advanced settings are being used - they're ignored otherwise, and
		// refusing to save over a field that's hidden would be confusing
		if (!deduplicate_threshold_valid(config.advanced.deduplicate_threshold)) {
			add_error(ValidationField::DEDUPLICATE_THRESHOLD, "deduplicate threshold must be a decimal number", true);

			if (fix)
				config.advanced.deduplicate_threshold = DEFAULT_CONFIG.advanced.deduplicate_threshold;
		}
	}

	if (!u::contains(SVP_INTERPOLATION_PRESETS, config.advanced.svp_interpolation_preset)) {
		add_error(
			ValidationField::SVP_INTERPOLATION_PRESET,
			std::format(
				"SVP interpolation preset ({}) is not a valid option", config.advanced.svp_interpolation_preset
			),
			true
		);

		if (fix)
			config.advanced.svp_interpolation_preset = DEFAULT_CONFIG.advanced.svp_interpolation_preset;
	}

	if (!u::contains(SVP_INTERPOLATION_ALGORITHMS, config.advanced.svp_interpolation_algorithm)) {
		add_error(
			ValidationField::SVP_INTERPOLATION_ALGORITHM,
			std::format(
				"SVP interpolation algorithm ({}) is not a valid option", config.advanced.svp_interpolation_algorithm
			),
			true
		);

		if (fix)
			config.advanced.svp_interpolation_algorithm = DEFAULT_CONFIG.advanced.svp_interpolation_algorithm;
	}

	if (!u::contains(INTERPOLATION_BLOCK_SIZES, config.advanced.interpolation_blocksize)) {
		add_error(
			ValidationField::INTERPOLATION_BLOCKSIZE,
			std::format("Interpolation block size ({}) is not a valid option", config.advanced.interpolation_blocksize),
			true
		);

		if (fix)
			config.advanced.interpolation_blocksize = DEFAULT_CONFIG.advanced.interpolation_blocksize;
	}

	return result;
}

BlurSettings config_blur::parse(const std::string& config_content) {
	std::istringstream stream(config_content);
	auto config_map = config_base::read_config_map(stream);
	return parse_from_map(config_map);
}

BlurSettings config_blur::parse(const std::filesystem::path& config_filepath) {
	auto settings = parse(config_base::read_config_file(config_filepath).value_or(""));

	// write formatted file
	create(config_filepath, settings);

	return settings;
}

BlurSettings config_blur::parse_from_map(const std::map<std::string, std::string>& config_map) {
	BlurSettings settings;

	config_base::extract_config_value(config_map, "blur", settings.blur);
	config_base::extract_config_value(config_map, "blur amount", settings.blur_amount);
	config_base::extract_config_value(config_map, "blur output fps", settings.blur_output_fps);
	config_base::extract_config_value(config_map, "blur weighting", settings.blur_weighting);
	config_base::extract_config_value(config_map, "blur gamma", settings.blur_gamma);

	config_base::extract_config_value(config_map, "interpolate", settings.interpolate);
	config_base::extract_config_value(config_map, "interpolated fps", settings.interpolated_fps);
	config_base::extract_config_value(config_map, "interpolation method", settings.interpolation_method);
	config_base::extract_config_value(config_map, "mask", settings.mask);
	if (settings.mask == masks::NONE_OPTION) // what the dropdown shows for no mask; it isn't a filename
		settings.mask.clear();
	config_base::extract_config_value(config_map, "auto mask", settings.auto_mask);

	config_base::extract_config_value(config_map, "pre-interpolate", settings.pre_interpolate);
	config_base::extract_config_value(config_map, "pre-interpolated fps", settings.pre_interpolated_fps);
	config_base::extract_config_value(config_map, "pre-interpolation method", settings.pre_interpolation_method);

	config_base::extract_config_value(config_map, "deduplicate", settings.deduplicate);
	config_base::extract_config_value(config_map, "deduplicate method", settings.deduplicate_method);

	config_base::extract_config_value(config_map, "encode preset", settings.encode_preset);
	config_base::extract_config_value(config_map, "quality", settings.quality);
	config_base::extract_config_value(config_map, "preview", settings.preview);
	config_base::extract_config_value(config_map, "detailed filenames", settings.detailed_filenames);
	config_base::extract_config_value(config_map, "copy dates", settings.copy_dates);
	config_base::extract_config_value(config_map, "upscale", settings.upscale);

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
		config_base::extract_config_value(config_map, "deduplicate threshold", settings.advanced.deduplicate_threshold);
		config_base::extract_config_value(config_map, "deduplicate real frame", settings.advanced.duplicate_timing);
		config_base::extract_config_value(
			config_map, "deduplicate max future checks", settings.advanced.max_future_checks
		);

		// 'deduplicate frames to interpolate' is what this used to be called, back when it named the frames to
		// interpolate between rather than the frame in a run that's real. the two it described that still exist
		// carry over; the other two hedged between them, which isn't offered any more, so they land on the default
		if (!config_map.contains("deduplicate real frame")) {
			std::string legacy;
			config_base::extract_config_value(config_map, "deduplicate frames to interpolate", legacy);

			if (legacy == "duplicate to next")
				settings.advanced.duplicate_timing = "last";
			else if (legacy == "previous to duplicate")
				settings.advanced.duplicate_timing = "first";
			else if (legacy.starts_with("surrounding frames"))
				settings.advanced.duplicate_timing = "surrounding";
		}

		config_base::extract_config_value(config_map, "video container", settings.advanced.video_container);
		config_base::extract_config_value(config_map, "custom ffmpeg filters", settings.advanced.ffmpeg_override);
		config_base::extract_config_value(config_map, "debug", settings.advanced.debug);
		config_base::extract_config_value(config_map, "resizing chroma location", settings.advanced.resize_chromaloc);
		config_base::extract_config_value(config_map, "source plugin", settings.advanced.source_plugin);

		config_base::extract_config_value(config_map, "auto mask samples", settings.advanced.auto_mask.samples);
		config_base::extract_config_value(config_map, "auto mask stillness", settings.advanced.auto_mask.stillness);
		config_base::extract_config_value(config_map, "auto mask fill", settings.advanced.auto_mask.fill);
		config_base::extract_config_value(config_map, "auto mask padding", settings.advanced.auto_mask.padding);
		config_base::extract_config_value(config_map, "auto mask feather", settings.advanced.auto_mask.feather);

		config_base::extract_config_value(
			config_map, "blur weighting gaussian std dev", settings.advanced.blur_weighting_gaussian_std_dev
		);
		config_base::extract_config_value(
			config_map, "blur weighting gaussian mean", settings.advanced.blur_weighting_gaussian_mean
		);
		config_base::extract_config_value(
			config_map, "blur weighting gaussian bound", settings.advanced.blur_weighting_gaussian_bound
		);

		config_base::extract_config_value(
			config_map, "svp interpolation preset", settings.advanced.svp_interpolation_preset
		);
		config_base::extract_config_value(
			config_map, "svp interpolation algorithm", settings.advanced.svp_interpolation_algorithm
		);
		config_base::extract_config_value(
			config_map, "interpolation block size", settings.advanced.interpolation_blocksize
		);
		config_base::extract_config_value(
			config_map, "interpolation mask area", settings.advanced.interpolation_mask_area
		);
		config_base::extract_config_value(config_map, "rife model", settings.advanced.rife_model);
#ifdef TENSORRT
		config_base::extract_config_value(config_map, "rife (tensorrt) model", settings.advanced.rife_trt_model);
#endif
		config_base::extract_config_value(config_map, "manual svp", settings.advanced.manual_svp);
		config_base::extract_config_value(config_map, "super string", settings.advanced.super_string);
		config_base::extract_config_value(config_map, "vectors string", settings.advanced.vectors_string);
		config_base::extract_config_value(config_map, "smooth string", settings.advanced.smooth_string);
	}

	u::verify_gpu_encoding(settings);
	u::set_fastest_devices(settings);

	return settings;
}

std::filesystem::path config_blur::get_configs_path() {
	return blur.settings_path / CONFIGS_FOLDER_NAME;
}

std::filesystem::path config_blur::get_config_path(const std::string& name) {
	return get_configs_path() / u::string_to_path(name + std::string(CONFIG_EXTENSION));
}

std::vector<std::string> config_blur::list() {
	std::vector<std::string> names;

	std::error_code ec; // don't throw if the folder's missing or unreadable, just show nothing
	for (const auto& entry : std::filesystem::directory_iterator(get_configs_path(), ec)) {
		if (!entry.is_regular_file(ec))
			continue;

		if (u::to_lower(u::path_to_string(entry.path().extension())) != CONFIG_EXTENSION)
			continue;

		names.push_back(u::path_to_string(entry.path().stem()));
	}

	std::ranges::sort(names);

	return names;
}

std::vector<std::string> config_blur::options(const std::string& current) {
	auto names = list();

	if (!current.empty() && !u::contains(names, current))
		names.push_back(current);

	return names;
}

BlurSettings config_blur::get_config(const std::string& name) {
	auto path = get_config_path(name);

	std::error_code ec;
	if (!name.empty() && std::filesystem::exists(path, ec))
		return parse(path);

	// the config's gone - deleted while a video sat in the queue holding its name, say. fall back rather
	// than render with something the user didn't ask for and can't see
	if (!name.empty())
		u::log("config '{}' not found, falling back to the default config", name);

	auto default_name = get_default_name();
	if (!default_name.empty() && default_name != name) {
		auto default_path = get_config_path(default_name);
		if (std::filesystem::exists(default_path, ec))
			return parse(default_path);
	}

	return DEFAULT_CONFIG;
}

void config_blur::save(const std::string& name, const BlurSettings& settings) {
	std::error_code ec;
	std::filesystem::create_directories(get_configs_path(), ec);

	create(get_config_path(name), settings);
}

void config_blur::remove(const std::string& name) {
	std::error_code ec;
	std::filesystem::remove(get_config_path(name), ec);

	if (ec)
		u::log("failed to remove config '{}': {}", name, ec.message());
}

std::string config_blur::get_default_name() {
	auto configured = config_app::get_app_config().default_config;
	if (configured.empty())
		return {};

	if (u::contains(list(), configured))
		return configured;

	// do not silently replace an invalid default with an arbitrary config
	return {};
}

std::string config_blur::resolve_config_name(
	const std::filesystem::path& input_path, const std::optional<std::string>& name_override
) {
	if (name_override && !name_override->empty())
		return *name_override;

	// a rule matching input_path's folder or filename picks the config here, once those exist
	(void)input_path;

	return get_default_name();
}

void config_blur::initialise_configs() {
	auto configs_path = get_configs_path();

	std::error_code ec;
	std::filesystem::create_directories(configs_path, ec);

	// before the empty check below, so an existing install's settings become its default config rather
	// than being left behind next to the folder while a fresh default is written inside it
	config_base::migrate_file(
		blur.settings_path / LEGACY_CONFIG_FILENAME, get_config_path(std::string(DEFAULT_CONFIG_NAME))
	);

	if (list().empty())
		save(std::string(DEFAULT_CONFIG_NAME), DEFAULT_CONFIG);
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
	j["mask"] = this->mask;
	j["auto_mask"] = this->auto_mask;

	j["pre_interpolate"] = this->pre_interpolate;
	j["pre_interpolated_fps"] = this->pre_interpolated_fps;
	j["pre_interpolation_method"] = this->pre_interpolation_method;

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
	j["upscale"] = this->upscale;
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
	j["duplicate_timing"] = this->advanced.duplicate_timing;
	j["max_future_checks"] = this->advanced.max_future_checks;

	// j["video_container"] = this->advanced.video_container;
	// j["ffmpeg_override"] = this->advanced.ffmpeg_override;
	j["debug"] = this->advanced.debug;
	j["resize_chromaloc"] = this->advanced.resize_chromaloc;
	j["source_plugin"] = this->advanced.source_plugin;

	j["auto_mask_samples"] = this->advanced.auto_mask.samples;
	j["auto_mask_stillness"] = this->advanced.auto_mask.stillness;
	j["auto_mask_fill"] = this->advanced.auto_mask.fill;
	j["auto_mask_padding"] = this->advanced.auto_mask.padding;
	j["auto_mask_feather"] = this->advanced.auto_mask.feather;

	j["blur_weighting_gaussian_std_dev"] = this->advanced.blur_weighting_gaussian_std_dev;
	j["blur_weighting_gaussian_mean"] = this->advanced.blur_weighting_gaussian_mean;
	j["blur_weighting_gaussian_bound"] = this->advanced.blur_weighting_gaussian_bound;

	j["svp_interpolation_preset"] = this->advanced.svp_interpolation_preset;
	j["svp_interpolation_algorithm"] = this->advanced.svp_interpolation_algorithm;
	j["interpolation_blocksize"] = this->advanced.interpolation_blocksize;
	j["interpolation_mask_area"] = this->advanced.interpolation_mask_area;

	// TODO: doing this here is stupid probably
	auto rife_model_path = get_rife_model_path();
	if (!rife_model_path)
		return tl::unexpected(rife_model_path.error());

	j["rife_model"] = *rife_model_path;

	j["rife_trt_model"] = this->advanced.rife_trt_model;

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
