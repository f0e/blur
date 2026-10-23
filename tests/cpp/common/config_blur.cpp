#include "helpers.h"

#include "common/config_app.h"
#include "common/config_blur.h"
#include "common/config_encoding_presets.h"

namespace {
	// everything off its default, so a setting the writer or the parser misses shows up. new settings go here too.
	// settings the parse checks against the system (gpu encoding, svp gpu rendering) are left where checking can't
	// change them
	BlurSettings changed_settings() {
		BlurSettings settings;

		settings.blur = false;
		settings.blur_amount = 1.5f;
		settings.blur_output_fps = 30;
		settings.blur_weighting = "gaussian_sym";
		settings.preserve_brightness = false;
		settings.bloom = true;
		settings.bloom_threshold = 0.5f;
		settings.bloom_strength = 0.75f;

		settings.interpolate = false;
		settings.interpolated_fps = "5x";
		settings.interpolation_method = "rife";
		settings.scene_detection = false;
		settings.mask = "hud.png";
		settings.auto_mask = true;
		settings.auto_mask_tracking = true;
		settings.auto_mask_layer = true;
		settings.auto_mask_inpainting = false;

		settings.pre_interpolate = true;
		settings.pre_interpolated_minimum_fps = 240;
		settings.pre_interpolated_minimum_multiplier = 2.f;
		settings.pre_interpolation_method = "svp";

		settings.deduplicate = false;
		settings.frame_timing_logs = false;
		settings.retime_method = "rife";

		settings.encode_preset = "h265";
		settings.quality = 20;
		settings.preview = false;
		settings.detailed_filenames = true;
		settings.copy_dates = true;
		settings.upscale = true;

		settings.optimise_gpu_settings = false;
		settings.svp_gpu_rendering = false;
		settings.svp_hud_mask = false;

		settings.lsfg_performance_mode = true;
		settings.lsfg_flow_scale = 0.5f;
		settings.lsfg_ui_detection = false;

		settings.gpu_decoding = true;

		settings.timescale = true;
		settings.input_timescale = 0.5f;
		settings.output_timescale = 2.f;
		settings.output_timescale_audio_pitch = true;

		settings.filters = true;
		settings.brightness = 1.25f;
		settings.saturation = 0.75f;
		settings.contrast = 1.5f;

		settings.override_advanced = true;
		auto& advanced = settings.advanced;
		advanced.deduplicate_range = 3;
		advanced.deduplicate_threshold = "0.01";
		advanced.duplicate_timing = "last";
		advanced.max_future_checks = 5;
		advanced.video_container = "mkv";
		advanced.ffmpeg_override = "-c:v libx264 -crf 20";
		advanced.debug = true;
		advanced.source_plugin = "BestSource";
		advanced.auto_mask.samples = 32;
		advanced.auto_mask.stillness = 0.75f;
		advanced.auto_mask.fill = 12;
		advanced.auto_mask.padding = 2;
		advanced.auto_mask.feather = 3;
		advanced.auto_mask.enclosed = false;
		advanced.auto_mask.enclosed_max_fps = 120;
		advanced.blur_sampling = "trapezoid";
		advanced.blur_weighting_gaussian_std_dev = 2.f;
		advanced.blur_weighting_gaussian_mean = 1.f;
		advanced.blur_weighting_gaussian_bound = "[0,3]";
		advanced.rife_model = "rife-v4.6";
		advanced.svp_interpolation_preset = "smooth";
		advanced.svp_interpolation_algorithm = "13";
		advanced.svp_search_size = "half";
		advanced.svp_block_overlap = "2";
		advanced.svp_block_size = "16";
		advanced.svp_mask_area = 50;
		advanced.manual_svp = true;
		advanced.super_string = "{pel:1}";
		advanced.vectors_string = "{block:{w:16}}";
		advanced.smooth_string = "{rate:{num:4}}";

		return settings;
	}
}

TEST(ConfigBlur, RoundTripsThroughItsFileFormat) {
	auto settings = changed_settings();

	EXPECT_EQ(config_blur::parse(config_blur::generate_config_string(settings, false)), settings);
}

TEST(ConfigBlur, RoundTripsTheDefaults) {
	EXPECT_EQ(config_blur::parse(config_blur::generate_config_string(BlurSettings{}, false)), BlurSettings{});
}

TEST(ConfigBlur, MissingSettingsKeepTheirDefaults) {
	auto settings = config_blur::parse(std::string("blur amount: 2\n"));

	BlurSettings expected;
	expected.blur_amount = 2.f;
	EXPECT_EQ(settings, expected);
}

TEST(ConfigBlur, AdvancedSettingsAreIgnoredWhenNotOverridden) {
	auto settings = config_blur::parse(std::string("advanced: false\nvideo container: mkv\n"));

	EXPECT_EQ(settings.advanced.video_container, BlurSettings{}.advanced.video_container);
}

TEST(ConfigBlur, NoneMaskMeansNoMask) {
	EXPECT_TRUE(config_blur::parse(std::string("mask: none\n")).mask.empty());
}

TEST(ConfigBlur, MigratesOldSettings) {
	auto settings = config_blur::parse(
		std::string(
			"[blur v2.45]\n"
			"blur gamma: 2\n"
			"pre-interpolated fps: 3x\n"
			"advanced: true\n"
			"deduplicate frames to interpolate: duplicate to next\n"
		)
	);

	EXPECT_TRUE(settings.preserve_brightness);
	EXPECT_EQ(settings.pre_interpolated_minimum_fps, 0);
	EXPECT_EQ(settings.pre_interpolated_minimum_multiplier, 3.f);
	EXPECT_EQ(settings.advanced.duplicate_timing, "last");
}

TEST(ConfigBlurValidation, DefaultsAreValid) {
	BlurSettings settings;

	EXPECT_TRUE(config_blur::validate(settings, GlobalAppSettings{}, EncodingPresetSettings{}, false).ok());
}

TEST(ConfigBlurValidation, FixesInvalidOptions) {
	BlurSettings settings;
	settings.override_advanced = true;
	settings.advanced.deduplicate_threshold = "abc";
	settings.advanced.svp_block_size = "7";

	auto result = config_blur::validate(settings, GlobalAppSettings{}, EncodingPresetSettings{}, true);

	ASSERT_EQ(result.errors.size(), 2);
	EXPECT_TRUE(std::ranges::all_of(result.errors, &config_blur::ValidationError::fixable));
	EXPECT_EQ(settings.advanced.deduplicate_threshold, BlurSettings{}.advanced.deduplicate_threshold);
	EXPECT_EQ(settings.advanced.svp_block_size, BlurSettings{}.advanced.svp_block_size);
}

TEST(ConfigBlurValidation, FixesAnOutOfRangeLsfgFlowScale) {
	for (float scale : { 0.f, -0.5f, 1.5f }) {
		BlurSettings settings;
		settings.lsfg_flow_scale = scale;

		auto result = config_blur::validate(settings, GlobalAppSettings{}, EncodingPresetSettings{}, true);

		ASSERT_EQ(result.errors.size(), 1) << scale;
		EXPECT_EQ(result.errors[0].field, config_blur::ValidationField::LSFG_FLOW_SCALE);
		EXPECT_EQ(settings.lsfg_flow_scale, BlurSettings{}.lsfg_flow_scale);
	}
}

TEST(ConfigBlurValidation, AudioCopyCantTimescale) {
	BlurSettings settings;
	settings.timescale = true;
	settings.output_timescale = 0.5f;
	settings.advanced.ffmpeg_override = "-c:v libx264 -c:a copy";

	auto result = config_blur::validate(settings, GlobalAppSettings{}, EncodingPresetSettings{}, true);

	ASSERT_EQ(result.errors.size(), 1);
	EXPECT_EQ(result.errors[0].field, config_blur::ValidationField::FFMPEG_OVERRIDE);
	EXPECT_FALSE(result.errors[0].fixable);
}
