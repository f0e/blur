#include "output.h"
#include "common/config_encoding_presets.h"
#include "common/encoding.h"

std::vector<std::string> rendering::build_encoding_args(
	const BlurSettings& settings, const GlobalAppSettings& app_settings, const EncodingPresetSettings* presets
) {
	if (!settings.advanced.ffmpeg_override.empty())
		return encoding::ffmpeg_string_to_args(settings.advanced.ffmpeg_override);

	std::string gpu_type = settings.gpu_encoding ? app_settings.gpu_type : "cpu";
	std::string preset = u::to_lower(settings.encode_preset.empty() ? "h264" : settings.encode_preset);

	if (presets)
		return config_encoding_presets::get_preset_params(*presets, gpu_type, preset, settings.quality);

	return config_encoding_presets::get_preset_params(gpu_type, preset, settings.quality);
}

bool rendering::wants_audio_copy(const std::vector<std::string>& encoding_args) {
	for (size_t i = 0; i + 1 < encoding_args.size(); i++) {
		const auto& flag = encoding_args[i];
		bool is_audio_codec = flag == "-acodec" || flag.starts_with("-c:a") || flag.starts_with("-codec:a");

		if (is_audio_codec && encoding_args[i + 1] == "copy")
			return true;
	}

	return false;
}

tl::expected<std::vector<std::string>, std::string> rendering::build_output_encoding_args(
	const media::VideoInfo& video_info, const BlurSettings& settings, const GlobalAppSettings& app_settings
) {
	auto encoding_args = build_encoding_args(settings, app_settings, nullptr);

	if (!video_info.audio_sample_rates.empty() && wants_audio_copy(encoding_args)) {
		if (auto conflict = get_audio_copy_conflict(settings, false))
			return tl::unexpected(*conflict);
	}

	return encoding_args;
}

bool rendering::copies_audio(const BlurSettings& settings, const GlobalAppSettings& app_settings) {
	return wants_audio_copy(build_encoding_args(settings, app_settings, nullptr));
}

bool rendering::copies_audio(
	const BlurSettings& settings, const GlobalAppSettings& app_settings, const EncodingPresetSettings& presets
) {
	return wants_audio_copy(build_encoding_args(settings, app_settings, &presets));
}

std::optional<std::string> rendering::get_audio_copy_conflict(const BlurSettings& settings, bool trimming) {
	std::optional<std::string> unsupported;
	if (trimming)
		unsupported = "trimming";
	else if (settings.timescale && settings.output_timescale != settings.input_timescale)
		unsupported = "timescale";

	if (!unsupported)
		return {};

	return std::format("{} needs to re-encode the audio, which '-c:a copy' doesn't allow", *unsupported);
}

size_t rendering::get_skipped_frames(
	const BlurSettings& settings, const GlobalAppSettings& app_settings, const media::VideoInfo& video_info
) {
	if (!app_settings.fully_blur_first_frame || !settings.blur || settings.blur_amount <= 0.f)
		return 0;

	// copied audio can't be trimmed to match
	if (!video_info.audio_sample_rates.empty() && copies_audio(settings, app_settings))
		return 0;

	// each frame blends up to blur_amount / 2 frames either side of it, but the first frames have nothing before them
	return static_cast<size_t>(std::ceil(settings.blur_amount / 2.f));
}

tl::expected<std::filesystem::path, std::string> rendering::build_output_filename(
	const std::filesystem::path& input_path, const BlurSettings& settings, const GlobalAppSettings& app_settings
) {
	auto output_folder = (input_path.parent_path() / app_settings.output_prefix).lexically_normal();

	try {
		std::filesystem::create_directories(output_folder);
	}
	catch (const std::filesystem::filesystem_error& e) {
		return tl::unexpected(fmt::format("Failed to create output directory: {}", e.what()));
	}

	std::string base_name = std::format("{} - blur", input_path.stem());

	if (settings.detailed_filenames) {
		std::string details;
		if (settings.blur && settings.interpolate) {
			details = std::format(
				"{}fps ({}, {})", settings.blur_output_fps, settings.interpolated_fps, settings.blur_amount
			);
		}
		else if (settings.blur) {
			details = std::format("{}fps ({})", settings.blur_output_fps, settings.blur_amount);
		}
		else if (settings.interpolate) {
			details = std::format("{}fps", settings.interpolated_fps);
		}

		if (!details.empty())
			base_name += " ~ " + details;
	}

	// find unique filename
	int counter = 1;
	std::filesystem::path result;
	do {
		std::string filename = base_name;
		if (counter > 1)
			filename += std::format(" ({})", counter);
		filename += "." + settings.advanced.video_container;
		result = output_folder / filename;
		counter++;
	}
	while (std::filesystem::exists(result));

	return result;
}
