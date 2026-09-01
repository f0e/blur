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
	std::string ffmpeg_override;
	bool debug = false;
	std::string resize_chromaloc = "default";
	std::string source_plugin = "LWLibavSource";

	float blur_weighting_gaussian_std_dev = 1.f;
	float blur_weighting_gaussian_mean = 2.f;
	std::string blur_weighting_gaussian_bound = "[0,2]";

	std::string svp_interpolation_preset = "weak";
	std::string svp_interpolation_algorithm = "13";
	std::string interpolation_blocksize = "8";
	int interpolation_mask_area = 0;
	std::string rife_model = "rife-v4.26_ensembleFalse";
	std::string rife_trt_model = "v4.26";

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
	float blur_gamma = 1.f;

	bool interpolate = true;
#ifdef __APPLE__
	std::string interpolated_fps = "600";
	std::string interpolation_method = "rife";
#else
	std::string interpolated_fps = "1200";
	std::string interpolation_method = "svp";
#endif

	// the base mask: filename of an image in <settings path>/masks, or empty for none. this is the mask that's
	// the same for every video - a game's HUD, say
	std::string mask;

	// work a second mask out from each video by finding the parts of its frame that never move, and apply it
	// over the base mask. catches whatever a particular video has that the base mask doesn't cover
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

	bool override_advanced = false;
	AdvancedSettings advanced;

public:
	BlurSettings();

	bool operator==(const BlurSettings& other) const = default;

	void verify_gpu_encoding();

	[[nodiscard]] tl::expected<nlohmann::json, std::string> to_json() const;

	[[nodiscard]] tl::expected<std::filesystem::path, std::string> get_rife_model_path() const;
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

	inline const std::vector<std::string> RESIZE_CHROMA_LOCATIONS = {
		"default", "left", "center", "top_left", "top", "bottom_left", "bottom",
	};

	inline const std::vector<std::string> SOURCE_PLUGINS = { "LWLibavSource", "BestSource" };

	// blur configs live one to a file in <settings path>/configs, the way masks live one to a file in
	// <settings path>/masks. a config's name is its filename without the extension, so the folder stays
	// readable and hand-editable, and a single config can be copied between machines on its own.
	//
	// which one new videos start on is the default, named in the app config rather than fixed to a
	// filename, so the default can be renamed like any other
	//
	// constexpr, not const std::string: GlobalAppSettings defaults its config name from DEFAULT_CONFIG_NAME,
	// and config_app::DEFAULT_CONFIG is itself a global. a dynamically initialised std::string here would be
	// read by that one before it was constructed, depending on which translation unit went first
	inline constexpr std::string_view CONFIGS_FOLDER_NAME = "configs";
	inline constexpr std::string_view CONFIG_EXTENSION = ".cfg";
	inline constexpr std::string_view DEFAULT_CONFIG_NAME = "default";

	// the one global config there used to be, migrated into the folder above on startup
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
	BlurSettings parse_from_map(const std::map<std::string, std::string>& config_map);

	std::filesystem::path get_configs_path();
	std::filesystem::path get_config_path(const std::string& name);

	// names of every config in the folder, sorted. empty if the folder doesn't exist
	std::vector<std::string> list();

	// what a config dropdown shows. `current` is kept in the list even if it's been deleted since it was
	// picked, so that's visible instead of the dropdown silently snapping to a different config
	std::vector<std::string> options(const std::string& current);

	// the named config. falls back to the default config, then to built-in defaults, if it's not there -
	// a config can be deleted out from under a video that was queued with it
	BlurSettings get_config(const std::string& name);

	void save(const std::string& name, const BlurSettings& settings);
	void remove(const std::string& name);

	// returns empty when no default is set or the configured default no longer exists
	std::string get_default_name();

	// where a video's config came from, so the queue can say why it picked the one it did
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

	// makes the configs folder, moves the old single global config into it, and makes sure at least one
	// config exists to select. called once on startup
	void initialise_configs();
}
