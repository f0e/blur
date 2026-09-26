#pragma once

struct GlobalAppSettings;
struct EncodingPresetSettings;

struct AutoMaskSettings {
	int samples = 48;

	float stillness = 0.9f;
	int fill = 24;
	int padding = 1;
	int feather = 1;

	bool operator==(const AutoMaskSettings& other) const = default;
};

struct AdvancedSettings {
	std::string video_container = "mp4";
	int deduplicate_range = 5;
	std::string deduplicate_threshold = "0.003";
	std::string duplicate_timing = "first";
	int max_future_checks = 3;
	bool frame_timing_logs = true;
	std::string ffmpeg_override;
	bool debug = false;
	std::string source_plugin = "LWLibavSource";

	float blur_weighting_gaussian_std_dev = 1.f;
	float blur_weighting_gaussian_mean = 2.f;
	std::string blur_weighting_gaussian_bound = "[0,2]";

	std::string svp_interpolation_preset = "weak";
	std::string svp_interpolation_algorithm = "13";
	std::string interpolation_blocksize = "8";
	int interpolation_mask_area = 0;

	AutoMaskSettings auto_mask;

	bool manual_svp = false;
	std::string super_string;
	std::string vectors_string;
	std::string smooth_string;

	bool operator==(const AdvancedSettings& other) const = default;
};

struct BlurSettings {
	bool blur = true;
	float blur_amount = 1.f;
	int blur_output_fps = 60;
	std::string blur_weighting = "equal";

	bool preserve_brightness = false;

	bool bloom = false;
	float bloom_threshold = 0.75f;
	float bloom_strength = 0.25f;

	bool interpolate = true;
#ifdef __APPLE__
	std::string interpolated_fps = "600";
	std::string interpolation_method = "rife";
#else
	std::string interpolated_fps = "1200";
	std::string interpolation_method = "svp";
#endif

	// filename of an image in the masks folder, empty for none
	std::string mask;

	// generate a mask from the parts of each video that never move, applied on top of `mask`
	bool auto_mask = false;

	bool pre_interpolate = false;
	std::string pre_interpolated_fps = "360";
	std::string pre_interpolation_method = "rife";

	bool timescale = false;
	float input_timescale = 1.f;
	float output_timescale = 1.f;
	bool output_timescale_audio_pitch = false;

	bool filters = false;
	float brightness = 1.f;
	float saturation = 1.f;
	float contrast = 1.f;

	std::string encode_preset = "h264";
	int quality = 16;
	bool upscale = false;

	bool deduplicate = true;
#ifdef __APPLE__
	std::string deduplicate_method = "rife";
#else
	std::string deduplicate_method = "svp";
#endif

	bool preview = true;
	bool detailed_filenames = false;
	bool copy_dates = false;

	bool gpu_decoding = false;
	bool gpu_interpolation = true;
	bool gpu_encoding = false;

	std::string rife_model = "rife-v4.26_ensembleFalse";
	std::string rife_trt_model = "rife_v4.26";

	bool override_advanced = false;
	AdvancedSettings advanced;

public:
	BlurSettings();

	bool operator==(const BlurSettings& other) const = default;

	void verify_gpu_encoding();

	[[nodiscard]] nlohmann::json to_json() const;

	[[nodiscard]] bool uses_interpolation_method(const std::string& method) const;
};

namespace config_blur {
	inline const BlurSettings DEFAULT_CONFIG;

	inline const std::vector<std::string> SVP_INTERPOLATION_PRESETS = {
		"weak", "film", "smooth", "animation", "default", "test",
	};

	inline const std::vector<std::string> SVP_INTERPOLATION_ALGORITHMS = {
		"1", "2", "11", "13", "21", "23",
	};

	inline const std::vector<std::string> INTERPOLATION_BLOCK_SIZES = { "4", "8", "16", "32" };

	inline const std::vector<std::string> SOURCE_PLUGINS = { "LWLibavSource", "BestSource" };

	// constexpr so they're initialised before config_rules::DEFAULT_CONFIG, which reads DEFAULT_CONFIG_NAME
	inline constexpr std::string_view CONFIGS_FOLDER_NAME = "configs";
	inline constexpr std::string_view CONFIG_EXTENSION = ".cfg";
	inline constexpr std::string_view DEFAULT_CONFIG_NAME = "default";

	const std::string LEGACY_CONFIG_FILENAME = ".blur-config.cfg";

	std::string generate_config_string(const BlurSettings& settings, bool concise);

	bool same_masking(const BlurSettings& a, const BlurSettings& b);

	void create(const std::filesystem::path& filepath, const BlurSettings& current_settings = BlurSettings());

	std::string export_concise(const BlurSettings& settings);

	enum class ValidationField : std::uint8_t {
		ENCODE_PRESET,
		DEDUPLICATE_THRESHOLD,
		FFMPEG_OVERRIDE,
		SVP_INTERPOLATION_PRESET,
		SVP_INTERPOLATION_ALGORITHM,
		INTERPOLATION_BLOCKSIZE,
	};

	struct ValidationError {
		ValidationField field;
		std::string message;
		bool fixable = false;
	};

	struct ValidationResult {
		std::vector<ValidationError> errors;

		[[nodiscard]] bool ok() const {
			return errors.empty();
		}

		[[nodiscard]] std::string message(bool fixable_only = false) const;
	};

	ValidationResult validate(
		BlurSettings& config, const GlobalAppSettings& app_settings, const EncodingPresetSettings& presets, bool fix
	);

	BlurSettings parse(const std::string& config_content);
	BlurSettings parse(const std::filesystem::path& config_filepath);
	BlurSettings parse_from_map(
		std::map<std::string, std::string> config_map, const std::optional<std::string>& config_version
	);

	std::filesystem::path get_configs_path();
	std::filesystem::path get_config_path(const std::string& name);

	std::vector<std::string> list();

	// includes `current` even if it's since been deleted
	std::vector<std::string> options(const std::string& current);

	// falls back to the default config, then the built-in defaults
	BlurSettings get_config(const std::string& name);

	void save(const std::string& name, const BlurSettings& settings);
	void remove(const std::string& name);

	// returns empty when no default is set or the configured default no longer exists
	std::string get_default_name();

	enum class ConfigSource : std::uint8_t {
		NONE,
		OVERRIDE,
		RULE,
		DEFAULT,
	};

	struct ResolvedConfig {
		std::string name;
		ConfigSource source = ConfigSource::NONE;
		std::string rule_pattern; // only set when source is RULE
	};

	ResolvedConfig resolve_config(
		const std::filesystem::path& input_path, const std::optional<std::string>& name_override
	);

	// returns empty when neither an override, a rule nor the default resolves
	std::string resolve_config_name(
		const std::filesystem::path& input_path, const std::optional<std::string>& name_override
	);

	void initialise_configs();
}
