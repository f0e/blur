#include "helpers.h"

#include "common/config_encoding_presets.h"

namespace {
	using Preset = EncodingPresetSettings::Preset;

	EncodingPresetSettings with_cpu_presets(std::vector<Preset> custom) {
		EncodingPresetSettings settings;
		auto* cpu = settings.find_preset_group("cpu");
		cpu->insert(cpu->end(), custom.begin(), custom.end());
		return settings;
	}
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
