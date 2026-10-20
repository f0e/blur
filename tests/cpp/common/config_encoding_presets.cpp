#include "helpers.h"

#include "common/config_encoding_presets.h"

TEST(EncodingPresets, FindsTheVideoCodec) {
	EXPECT_EQ(config_encoding_presets::extract_codec_from_args({ "-c:v", "libx265" }), "libx265");
	EXPECT_EQ(config_encoding_presets::extract_codec_from_args({ "-codec:video", "ffv1" }), "ffv1");
	EXPECT_EQ(config_encoding_presets::extract_codec_from_args({ "-c:v", "libx264", "-c:v", "libx265" }), "libx265");

	EXPECT_FALSE(config_encoding_presets::extract_codec_from_args({}));
	EXPECT_FALSE(config_encoding_presets::extract_codec_from_args({ "-c:v" }));
	EXPECT_FALSE(config_encoding_presets::extract_codec_from_args({ "-c:a", "aac" }));
}
