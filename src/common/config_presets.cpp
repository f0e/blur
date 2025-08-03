#include "config_presets.h"
#include "config_base.h"

namespace {
	std::vector<std::string> get_ffmpeg_args(std::string params_str, int quality) {
		// replace quality placeholder
		params_str = u::replace_all(params_str, "{quality}", std::to_string(quality));

		return u::ffmpeg_string_to_args(params_str);
	}
}

void config_presets::create(const std::filesystem::path& filepath, const PresetSettings& current_settings) {
	std::ofstream output(filepath);

	output << "[blur v" << BLUR_VERSION << "]" << "\n";
	output << "* = default preset, cannot be modified" << "\n";

	for (const auto& gpu_presets : current_settings.all_gpu_presets) {
		output << "\n";
		output << "- " << gpu_presets.gpu_type << "\n";

		for (const auto& preset : gpu_presets.presets) {
			if (preset.is_default)
				output << "*";

			output << preset.name << ": " << preset.args << "\n";
		}
	}
}

PresetSettings config_presets::parse(const std::filesystem::path& config_filepath) {
	PresetSettings settings = DEFAULT_CONFIG;

	std::ifstream file(config_filepath);
	if (!file)
		return settings; // defaults if file couldn't be opened

	std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	std::istringstream stream(content);
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

			// new gpu type
			if (!current_presets) {
				auto& new_entry = settings.all_gpu_presets.emplace_back(
					PresetSettings::GpuPresets{
						.gpu_type = current_gpu_type,
						.presets = {},
					}
				);
				current_presets = &new_entry.presets;
			}

			continue;
		}

		size_t delimiter_pos = line.find(':');
		if (delimiter_pos != std::string::npos && current_presets) {
			std::string preset_name = u::trim(line.substr(0, delimiter_pos));

			if (!preset_name.empty() && preset_name[0] == '*') {
				// default preset (or imposter), skip
				DEBUG_LOG("skipping default preset (line: {})", line);
				continue;
			}

			// don't allow presets with same name as defaults (e.g. h265)
			bool is_default_name = false;
			for (const auto& preset : *current_presets) {
				if (preset.name == preset_name && preset.is_default) {
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
				if (preset.name == preset_name) {
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

	// recreate the config file
	create(config_filepath, settings);

	return settings;
}

std::filesystem::path config_presets::get_preset_config_path() {
	return blur.settings_path / PRESET_CONFIG_FILENAME;
}

PresetSettings config_presets::get_preset_config() {
	using namespace std::chrono;
	static constexpr auto reload_interval = seconds(5);
	static PresetSettings cached;
	static auto last_reload_time = steady_clock::now() - reload_interval;

	auto now = steady_clock::now();
	if (now - last_reload_time >= reload_interval) {
		cached = config_base::load_config<PresetSettings>(get_preset_config_path(), create, parse);
		last_reload_time = now;
	}

	return cached;
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

// NOLINTBEGIN(misc-no-recursion) trust me bro
std::vector<std::string> config_presets::get_preset_params(
	const std::string& gpu_type, const std::string& preset, int quality
) {
	PresetSettings config = get_preset_config();
	const std::string* params_ptr = config.find_preset_params(gpu_type, preset);

	if (params_ptr) {
		return get_ffmpeg_args(*params_ptr, quality);
	}

	if (gpu_type != "cpu") {
		return get_preset_params("cpu", preset, quality);
	}

	return get_preset_params("cpu", "h264", quality);
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
		config.min_quality = 0;
		config.max_quality = 51;
		config.quality_label = "(0: lossless, 23: balanced, 51: worst)";
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
