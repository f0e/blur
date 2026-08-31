#pragma once

struct EncodingPresetSettings {
	struct Preset {
		std::string name;
		std::string args;
		bool is_default = false;

		bool operator==(const Preset& other) const = default;
	};

	struct GpuPresets {
		std::string gpu_type;
		std::vector<Preset> presets;

		bool operator==(const GpuPresets& other) const = default;
	};

	std::vector<GpuPresets> all_gpu_presets = {
		{
			.gpu_type="nvidia",
			.presets={
				{ .name = "h264", .args = "-c:v h264_nvenc -preset p5 -rc vbr -cq {quality} -c:a aac -b:a 320k -movflags +faststart", .is_default = true },
				{ .name = "h265", .args = "-c:v hevc_nvenc -preset p5 -rc vbr -cq {quality} -c:a aac -b:a 320k -movflags +faststart", .is_default = true },
				{ .name = "av1", .args = "-c:v av1_nvenc -preset p5 -rc vbr -cq {quality} -c:a aac -b:a 320k -movflags +faststart", .is_default = true },
			},
		},
		{
			.gpu_type="amd",
			.presets={
				{ .name = "h264",
							.args = "-c:v h264_amf -rc cqp -qp_i {quality} -qp_p {quality} -qp_b {quality} -c:a aac -b:a 320k -movflags +faststart",
							.is_default = true },
				{ .name = "h265",
							.args = "-c:v hevc_amf -rc cqp -qp_i {quality} -qp_p {quality} -qp_b {quality} -c:a aac -b:a 320k -movflags +faststart",
							.is_default = true },
				{ .name = "av1",
							.args = "-c:v av1_amf -rc cqp -qp_i {quality} -qp_p {quality} -qp_b {quality} -c:a aac -b:a 320k -movflags +faststart",
							.is_default = true },
			},
		},
		{
			.gpu_type="intel",
			.presets={
				{ .name = "h264",
		          .args = "-c:v h264_qsv -global_quality {quality} -preset veryfast -c:a aac -b:a 320k -movflags +faststart",
		          .is_default = true },
				{ .name = "h265",
		          .args = "-c:v hevc_qsv -global_quality {quality} -preset veryfast -c:a aac -b:a 320k -movflags +faststart",
		          .is_default = true },
				{ .name = "av1",
		          .args = "-c:v av1_qsv -global_quality {quality} -preset veryfast -c:a aac -b:a 320k -movflags +faststart",
		          .is_default = true },
			},
		},
		{
			.gpu_type="mac",
			.presets={
				{ .name = "h264", .args = "-c:v h264_videotoolbox -q:v {quality} -c:a aac -b:a 320k -movflags +faststart", .is_default = true },
				{ .name = "h265", .args = "-c:v hevc_videotoolbox -q:v {quality} -c:a aac -b:a 320k -movflags +faststart", .is_default = true },
				{ .name = "av1", .args = "-c:v av1_videotoolbox -q:v {quality} -c:a aac -b:a 320k -movflags +faststart", .is_default = true },
				{ .name = "prores", .args = "-c:v prores_videotoolbox -profile:v {quality} -c:a aac -b:a 320k -movflags +faststart", .is_default = true },
			},
		},
		{
			.gpu_type="cpu",
			.presets={
				{ .name = "h264", .args = "-c:v libx264 -preset veryfast -crf {quality} -c:a aac -b:a 320k -movflags +faststart", .is_default = true },
				{ .name = "h265", .args = "-c:v libx265 -preset veryfast -crf {quality} -c:a aac -b:a 320k -movflags +faststart", .is_default = true },
				{ .name = "av1", .args = "-c:v libaom-av1 -cpu-used 4 -crf {quality} -c:a aac -b:a 320k -movflags +faststart", .is_default = true },
				{ .name = "vp9", .args = "-c:v libvpx-vp9 -deadline good -crf {quality} -b:v 0 -c:a aac -b:a 320k -movflags +faststart", .is_default = true },
			},
		},
	};

	bool operator==(const EncodingPresetSettings& other) const = default;

	[[nodiscard]] const std::string* find_preset_params(
		const std::string& gpu_type, const std::string& preset_name
	) const {
		const auto* presets = find_preset_group(gpu_type);
		if (!presets)
			return nullptr;

		for (const auto& preset : *presets) {
			if (u::to_lower(preset.name) == u::to_lower(preset_name)) {
				return &preset.args;
			}
		}

		return nullptr;
	}

	[[nodiscard]] const std::vector<Preset>* find_preset_group(const std::string& gpu_type) const {
		for (const auto& gpu_presets : all_gpu_presets) {
			if (gpu_presets.gpu_type == gpu_type) {
				return &gpu_presets.presets;
			}
		}
		return nullptr;
	}

	[[nodiscard]] std::vector<Preset>* find_preset_group(const std::string& gpu_type) {
		for (auto& gpu_presets : all_gpu_presets) {
			if (gpu_presets.gpu_type == gpu_type) {
				return &gpu_presets.presets;
			}
		}
		return nullptr;
	}
};

namespace config_encoding_presets {
	inline const EncodingPresetSettings DEFAULT_CONFIG;

	const std::string CONFIG_FILENAME = "encoding-presets.cfg";

	// what it used to be called, migrated on startup (see Blur::initialise)
	const std::string LEGACY_CONFIG_FILENAME = "presets.cfg";

	std::string generate_config_string(const EncodingPresetSettings& settings);

	void create(
		const std::filesystem::path& filepath, const EncodingPresetSettings& current_settings = EncodingPresetSettings()
	);

	EncodingPresetSettings parse(const std::filesystem::path& config_filepath);
	EncodingPresetSettings parse(const std::string& config_content);

	std::filesystem::path get_config_path();

	EncodingPresetSettings get_config();

	void save(const EncodingPresetSettings& settings);

	struct PresetDetails {
		std::string name;
		std::string codec;
	};

	std::vector<PresetDetails> get_available_presets(bool gpu_encoding, const std::string& gpu_type);
	std::vector<PresetDetails> get_available_presets(
		const EncodingPresetSettings& config, bool gpu_encoding, const std::string& gpu_type
	);

	std::vector<std::string> get_preset_params(const std::string& gpu_type, const std::string& preset, int quality);
	std::vector<std::string> get_preset_params(
		const EncodingPresetSettings& config, const std::string& gpu_type, const std::string& preset, int quality
	);

	tl::expected<std::string, std::string> extract_codec_from_args(const std::vector<std::string>& ffmpeg_args);

	struct ValidationMessage {
		std::string message;
		bool is_error = false;
	};

	std::optional<ValidationMessage> validate(const std::vector<EncodingPresetSettings::Preset>& presets, size_t index);

	struct PresetError {
		std::string gpu_type;
		std::string message;
	};

	std::optional<PresetError> validate(
		const EncodingPresetSettings& settings
	); // @todo: should this return all erroring presets not just first?

	struct QualityConfig {
		int min_quality;
		int max_quality;
		std::string quality_label;
	};

	QualityConfig get_quality_config(const std::string& codec);
}
