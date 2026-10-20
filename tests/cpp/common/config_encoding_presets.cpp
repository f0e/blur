#include "helpers.h"

#include "common/config_encoding_presets.h"

namespace {
	using Preset = EncodingPresetSettings::Preset;
	using Args = std::vector<std::string>;

	EncodingPresetSettings with_cpu_presets(std::vector<Preset> custom) {
		EncodingPresetSettings settings;
		auto* cpu = settings.find_preset_group("cpu");
		cpu->insert(cpu->end(), custom.begin(), custom.end());
		return settings;
	}

	std::optional<config_encoding_presets::ValidationMessage> validate_last(std::vector<Preset> presets) {
		return config_encoding_presets::validate(presets, presets.size() - 1);
	}
}

TEST(EncodingPresets, RoundTripsThroughItsFileFormat) {
	auto settings = with_cpu_presets({ { .name = "lossless", .args = "-c:v libx264 -qp 0 -c:a copy" } });

	EXPECT_EQ(
		config_encoding_presets::parse(config_encoding_presets::generate_config_string(settings)), settings
	);
}

TEST(EncodingPresets, BuiltInPresetsCantBeChangedInTheFile) {
	auto settings = config_encoding_presets::parse(std::string("- cpu\n*h264: -c:v changed\nH264: -c:v changed\n"));

	EXPECT_EQ(settings, EncodingPresetSettings{});
}

TEST(EncodingPresets, ParsesCustomPresets) {
	auto settings = config_encoding_presets::parse(
		std::string(
			"- cpu\n"
			"fast: -c:v libx264 -preset ultrafast -crf {quality}\n"
			"FAST: -c:v libx264 -preset superfast -crf {quality}\n"
			"- nvidia\n"
			"nv: -c:v h264_nvenc\n"
			"- toaster\n"
			"bread: -c:v toast\n"
		)
	);

	const auto* cpu = settings.find_preset_params("cpu", "fast");
	ASSERT_TRUE(cpu);
	EXPECT_EQ(*cpu, "-c:v libx264 -preset superfast -crf {quality}");
	EXPECT_TRUE(settings.find_preset_params("nvidia", "nv"));
	EXPECT_FALSE(settings.find_preset_group("toaster"));
}

TEST(EncodingPresets, FillsInTheQuality) {
	auto settings = with_cpu_presets({ { .name = "two", .args = "-c:v libx264 -qmin {quality} -qmax {quality}" } });

	EXPECT_EQ(
		config_encoding_presets::get_preset_params(settings, "cpu", "two", 18),
		(Args{ "-c:v", "libx264", "-qmin", "18", "-qmax", "18" })
	);
}

TEST(EncodingPresets, FallsBackToCpuThenH264) {
	EncodingPresetSettings settings;
	auto cpu_vp9 = config_encoding_presets::get_preset_params(settings, "cpu", "vp9", 30);
	auto cpu_h264 = config_encoding_presets::get_preset_params(settings, "cpu", "h264", 30);

	EXPECT_EQ(config_encoding_presets::get_preset_params(settings, "nvidia", "vp9", 30), cpu_vp9);
	EXPECT_EQ(config_encoding_presets::get_preset_params(settings, "cpu", "missing", 30), cpu_h264);
	EXPECT_EQ(config_encoding_presets::get_preset_params(settings, "toaster", "missing", 30), cpu_h264);
}

TEST(EncodingPresets, ListsPresetsByCodec) {
	auto settings = with_cpu_presets(
		{
			{ .name = "old style", .args = "-vcodec libx264 -crf {quality}" },
			{ .name = "audio only", .args = "-c:a flac" },
		}
	);

	auto presets = config_encoding_presets::get_available_presets(settings, false, "nvidia");

	auto find = [&](const std::string& name) {
		return std::ranges::find(presets, name, &config_encoding_presets::PresetDetails::name);
	};

	ASSERT_NE(find("h264"), presets.end());
	EXPECT_EQ(find("h264")->codec, "libx264");
	ASSERT_NE(find("old style"), presets.end());
	EXPECT_EQ(find("old style")->codec, "libx264");
	EXPECT_EQ(find("audio only"), presets.end());

	EXPECT_EQ(config_encoding_presets::get_available_presets(settings, true, "nvidia")[0].codec, "h264_nvenc");
}

TEST(EncodingPresets, FindsTheVideoCodec) {
	EXPECT_EQ(config_encoding_presets::extract_codec_from_args({ "-c:v", "libx265" }), "libx265");
	EXPECT_EQ(config_encoding_presets::extract_codec_from_args({ "-codec:video", "ffv1" }), "ffv1");
	EXPECT_EQ(config_encoding_presets::extract_codec_from_args({ "-c:v", "libx264", "-c:v", "libx265" }), "libx265");

	EXPECT_FALSE(config_encoding_presets::extract_codec_from_args({}));
	EXPECT_FALSE(config_encoding_presets::extract_codec_from_args({ "-c:v" }));
	EXPECT_FALSE(config_encoding_presets::extract_codec_from_args({ "-c:a", "aac" }));
}

TEST(EncodingPresetValidation, RejectsBadNames) {
	auto error = [](const std::string& name) {
		auto message = validate_last({ { .name = name, .args = "-c:v libx264 -crf {quality}" } });
		return message && message->is_error;
	};

	EXPECT_TRUE(error(""));
	EXPECT_TRUE(error("  "));
	EXPECT_TRUE(error("a:b"));
	EXPECT_TRUE(error("-fast"));
	EXPECT_TRUE(error("*fast"));
	EXPECT_FALSE(error("fast"));
}

TEST(EncodingPresetValidation, RejectsDuplicateNames) {
	auto built_in = validate_last(
		{
			{ .name = "h264", .args = "-c:v libx264", .is_default = true },
			{ .name = "H264 ", .args = "-c:v libx264 -crf {quality}" },
		}
	);
	ASSERT_TRUE(built_in);
	EXPECT_TRUE(built_in->is_error);

	std::vector<Preset> presets = {
		{ .name = "fast", .args = "-c:v libx264 -crf {quality}" },
		{ .name = "fast", .args = "-c:v libx264 -crf {quality}" },
	};

	// only the later one of the pair is marked
	EXPECT_FALSE(config_encoding_presets::validate(presets, 0));
	ASSERT_TRUE(config_encoding_presets::validate(presets, 1));
	EXPECT_TRUE(config_encoding_presets::validate(presets, 1)->is_error);
}

TEST(EncodingPresetValidation, WarnsAboutPresetsThatWontWork) {
	auto no_args = validate_last({ { .name = "empty", .args = " " } });
	ASSERT_TRUE(no_args);
	EXPECT_TRUE(no_args->is_error);

	auto no_codec = validate_last({ { .name = "audio", .args = "-c:a flac" } });
	ASSERT_TRUE(no_codec);
	EXPECT_FALSE(no_codec->is_error);

	auto no_quality = validate_last({ { .name = "fixed", .args = "-c:v libx264 -crf 18" } });
	ASSERT_TRUE(no_quality);
	EXPECT_FALSE(no_quality->is_error);
}

TEST(EncodingPresetValidation, ChecksOnlyCustomPresets) {
	EXPECT_FALSE(config_encoding_presets::validate(EncodingPresetSettings{}));

	auto settings = with_cpu_presets({ { .name = "h265", .args = "-c:v libx265" } });

	auto error = config_encoding_presets::validate(settings);
	ASSERT_TRUE(error);
	EXPECT_EQ(error->gpu_type, "cpu");
}
