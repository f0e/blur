#pragma once

struct GlobalAppSettings;
struct PresetSettings;

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

	const std::string CONFIG_FILENAME = ".blur-config.cfg";

	std::string generate_config_string(const BlurSettings& settings, bool concise);

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
		BlurSettings& config, const GlobalAppSettings& app_settings, const PresetSettings& presets, bool fix
	);

	BlurSettings parse(const std::string& config_content);
	BlurSettings parse(const std::filesystem::path& config_filepath);
	BlurSettings parse_from_map(const std::map<std::string, std::string>& config_map);

	BlurSettings parse_global_config();

	std::filesystem::path get_global_config_path();
	std::filesystem::path get_config_filename(const std::filesystem::path& video_folder);
	BlurSettings get_global_config();

	struct ConfigRes {
		BlurSettings config;
		bool is_global = false;
	};

	ConfigRes get_config(const std::filesystem::path& config_filepath, bool use_global);
}
