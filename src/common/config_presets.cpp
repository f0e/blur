#include "config_presets.h"
#include "config_base.h"

namespace {
	constexpr auto CACHE_RELOAD_INTERVAL = std::chrono::seconds(5);

	std::mutex cache_mutex;
	PresetSettings cached_config;
	std::optional<std::chrono::steady_clock::time_point> cache_time;

	std::vector<std::string> get_ffmpeg_args(std::string params_str, int quality) {
		// replace quality placeholder
		params_str = u::replace_all(params_str, "{quality}", std::to_string(quality));

		return u::ffmpeg_string_to_args(params_str);
	}

	std::optional<std::string> get_preset_error(const std::vector<PresetSettings::Preset>& presets, size_t index) {
		const auto& preset = presets[index];

		std::string name = u::trim(preset.name);

		if (name.empty())
			return "enter a name";

		if (u::contains(name, ":"))
			return "names can't contain ':'";

		if (name.starts_with('-') || name.starts_with('*'))
			return "names can't start with '-' or '*'";

		for (size_t i = 0; i < presets.size(); i++) {
			if (i == index)
				continue;

			if (!presets[i].is_default && i > index)
				continue;

			if (u::to_lower(u::trim(presets[i].name)) == u::to_lower(name)) {
				return presets[i].is_default ? std::format("'{}' is a built-in preset", name) : "name already used";
			}
		}

		if (u::trim(preset.args).empty())
			return "enter ffmpeg arguments";

		return {};
	}

	std::optional<std::string> get_preset_warning(const PresetSettings::Preset& preset) {
		auto args = u::ffmpeg_string_to_args(preset.args);

		if (args.size() < 2 || !config_presets::extract_codec_from_args(args))
			return "no video codec (-c:v), this won't show up as an encode preset";

		if (!u::contains(preset.args, "{quality}"))
			return "no {quality}, the quality setting won't do anything";

		return {};
	}
}

std::optional<config_presets::ValidationMessage> config_presets::validate(
	const std::vector<PresetSettings::Preset>& presets, size_t index
) {
	if (auto error = get_preset_error(presets, index))
		return ValidationMessage{ .message = *error, .is_error = true };

	if (auto warning = get_preset_warning(presets[index]))
		return ValidationMessage{ .message = *warning };

	return {};
}

std::optional<config_presets::PresetError> config_presets::validate(const PresetSettings& settings) {
	for (const auto& gpu_presets : settings.all_gpu_presets) {
		for (size_t i = 0; i < gpu_presets.presets.size(); i++) {
			if (gpu_presets.presets[i].is_default)
				continue;

			if (auto error = get_preset_error(gpu_presets.presets, i))
				return PresetError{ .gpu_type = gpu_presets.gpu_type, .message = *error };
		}
	}

	return {};
}

std::string config_presets::generate_config_string(const PresetSettings& settings) {
	std::ostringstream output;

	output << "[blur v" << BLUR_VERSION << "]" << "\n";
	output << "* = default preset, cannot be modified" << "\n";

	for (const auto& gpu_presets : settings.all_gpu_presets) {
		output << "\n";
		output << "- " << gpu_presets.gpu_type << "\n";

		for (const auto& preset : gpu_presets.presets) {
			if (preset.is_default)
				output << "*";

			output << preset.name << ": " << preset.args << "\n";
		}
	}

	return output.str();
}

void config_presets::create(const std::filesystem::path& filepath, const PresetSettings& current_settings) {
	std::ofstream output(filepath);
	output << generate_config_string(current_settings);
}

PresetSettings config_presets::parse(const std::filesystem::path& config_filepath) {
	std::ifstream file(config_filepath);
	if (!file)
		return DEFAULT_CONFIG; // defaults if file couldn't be opened

	std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	auto settings = parse(content);

	// recreate the config file using the parsed values (keeps nice formatting)
	create(config_filepath, settings);

	return settings;
}

PresetSettings config_presets::parse(const std::string& config_content) {
	PresetSettings settings = DEFAULT_CONFIG;

	std::istringstream stream(config_content);
	std::string line;

	std::string current_gpu_type;
	std::vector<PresetSettings::Preset>* current_presets = nullptr;

	while (std::getline(stream, line)) {
		line = u::trim(line);

		if (line.empty() || line.front() == '[' || line.front() == '#') {
			continue;
		}

		if (line.front() == '-') {
			current_gpu_type = u::trim(line.substr(1));

			current_presets = nullptr;
			for (auto& gpu_presets : settings.all_gpu_presets) {
				if (gpu_presets.gpu_type == current_gpu_type) {
					current_presets = &gpu_presets.presets;
					break;
				}
			}

			continue;
		}

		size_t delimiter_pos = line.find(':');
		if (delimiter_pos != std::string::npos && current_presets) {
			std::string preset_name = u::trim(line.substr(0, delimiter_pos));

			if (!preset_name.empty() && preset_name.front() == '*') {
				preset_name.erase(0, 1);
			}

			// don't allow presets with same name as defaults (e.g. h265)
			bool is_default_name = false;
			for (const auto& preset : *current_presets) {
				if (preset.is_default && u::to_lower(preset.name) == u::to_lower(preset_name)) {
					is_default_name = true;
					break;
				}
			}

			if (is_default_name) {
				DEBUG_LOG("skipping preset with default name (line: {})", line);
				continue;
			}

			std::string preset_params = u::trim(line.substr(delimiter_pos + 1));

			bool found = false;
			for (auto& preset : *current_presets) {
				if (u::to_lower(preset.name) == u::to_lower(preset_name)) {
					preset.args = preset_params; // already exists - update
					found = true;
					break;
				}
			}

			// new preset
			if (!found) {
				current_presets->emplace_back(
					PresetSettings::Preset{
						.name = preset_name,
						.args = preset_params,
					}
				);
			}
		}
	}

	return settings;
}

std::filesystem::path config_presets::get_preset_config_path() {
	return blur.settings_path / PRESET_CONFIG_FILENAME;
}

PresetSettings config_presets::get_preset_config() {
	std::lock_guard lock(cache_mutex);

	auto now = std::chrono::steady_clock::now();
	if (!cache_time || now - *cache_time >= CACHE_RELOAD_INTERVAL) {
		cached_config = config_base::load_config<PresetSettings>(get_preset_config_path(), create, parse);
		cache_time = now;
	}

	return cached_config;
}

void config_presets::save(const PresetSettings& settings) {
	create(get_preset_config_path(), settings);

	std::lock_guard lock(cache_mutex);
	cached_config = settings;
	cache_time = std::chrono::steady_clock::now();
}

std::vector<config_presets::PresetDetails> config_presets::get_available_presets(
	bool gpu_encoding, const std::string& gpu_type
) {
	std::vector<PresetDetails> available_presets;

	std::string type_to_check = gpu_encoding ? gpu_type : "cpu";

	PresetSettings config = get_preset_config();
	const auto* preset_group = config.find_preset_group(type_to_check);

	if (preset_group) {
		for (const auto& preset : *preset_group) {
			auto params = get_ffmpeg_args(preset.args, 0);

			for (auto it = params.rbegin(); it != params.rend(); ++it) {
				if (it == params.rbegin())
					continue;

				if (*it == "-c:v" || *it == "-codec:v") {
					std::string codec = *(it - 1);
					available_presets.push_back(
						{
							.name = preset.name,
							.codec = codec,
						}
					);
					break;
				}
			}
		}
	}

	return available_presets;
}

std::vector<std::string> config_presets::get_preset_params(
	const std::string& gpu_type, const std::string& preset, int quality
) {
	return get_preset_params(get_preset_config(), gpu_type, preset, quality);
}

// NOLINTBEGIN(misc-no-recursion) trust me bro
std::vector<std::string> config_presets::get_preset_params(
	const PresetSettings& config, const std::string& gpu_type, const std::string& preset, int quality
) {
	const std::string* params_ptr = config.find_preset_params(gpu_type, preset);

	if (params_ptr) {
		return get_ffmpeg_args(*params_ptr, quality);
	}

	if (gpu_type != "cpu") {
		return get_preset_params(config, "cpu", preset, quality);
	}

	return get_preset_params(config, "cpu", "h264", quality);
}

// NOLINTEND(misc-no-recursion)

tl::expected<std::string, std::string> config_presets::extract_codec_from_args(
	const std::vector<std::string>& ffmpeg_args
) {
	const std::vector<std::string> codec_flags = { "-c:v", "-codec:v", "-vcodec", "-c:video", "-codec:video" };

	for (size_t i = 0; i < ffmpeg_args.size() - 1; i++) {
		for (const auto& flag : codec_flags) {
			if (ffmpeg_args[i] == flag) {
				return ffmpeg_args[i + 1];
			}
		}
	}

	return tl::unexpected("no codec found");
}

config_presets::QualityConfig config_presets::get_quality_config(const std::string& codec) {
	QualityConfig config;

	// Detect codec type and set appropriate ranges
	if (codec == "h264_nvenc" || codec == "hevc_nvenc" || codec == "av1_nvenc") {
		// NVIDIA NVENC
		config.min_quality = 1;
		config.max_quality = 51;
		config.quality_label = "(1: best, 23: balanced, 51: worst)\n";
	}
	else if (codec == "h264_amf" || codec == "hevc_amf" || codec == "av1_amf") {
		// AMD AMF
		config.min_quality = 0;
		config.max_quality = 51;
		config.quality_label = "(0: best, 23: balanced, 51: worst)";
	}
	else if (codec == "h264_qsv" || codec == "hevc_qsv" || codec == "av1_qsv") {
		// Intel QSV
		config.min_quality = 1;
		config.max_quality = 51;
		config.quality_label = "(1: best, 23: balanced, 51: worst)";
	}
	else if (codec == "h264_videotoolbox" || codec == "hevc_videotoolbox" || codec == "av1_videotoolbox") {
		// Mac VideoToolbox H264/H265/AV1
		config.min_quality = 0;
		config.max_quality = 100;
		config.quality_label = "(100: best, 0: worst)"; // todo: add balanced when i know what a good value is
	}
	else if (codec == "prores_videotoolbox") {
		// Mac ProRes
		config.min_quality = 0;
		config.max_quality = 4;
		config.quality_label = "(0: proxy, 1: lt, 2: std, 3: hq, 4: 4444xq)";
	}
	else if (codec == "libx264" || codec == "libx265") {
		// CPU x264/x265
		config.min_quality = 0;
		config.max_quality = 51;
		config.quality_label = "(0: lossless, 23: balanced, 51: worst)";
	}
	else if (codec == "libaom-av1" || codec == "libvpx-vp9") {
		// CPU AV1/VP9
		config.min_quality = 15;
		config.max_quality = 50;
		config.quality_label = "(15: best, 30: balanced, 50: worst)";
	}
	else {
		// Fallback
		config.min_quality = 0;
		config.max_quality = 51;
		config.quality_label = "";
	}

	return config;
}
