#include "helpers.h"

#include "common/encoding.h"

using Args = std::vector<std::string>;

TEST(FfmpegArgs, SplitsOnSpaces) {
	EXPECT_EQ(encoding::ffmpeg_string_to_args("-c:v libx264  -crf 18 "), (Args{ "-c:v", "libx264", "-crf", "18" }));
}

TEST(FfmpegArgs, QuotesKeepSpaces) {
	EXPECT_EQ(
		encoding::ffmpeg_string_to_args(R"(-vf "scale=1280:-2, fps=60" -metadata title="a b")"),
		(Args{ "-vf", "scale=1280:-2, fps=60", "-metadata", "title=a b" })
	);
}

TEST(FfmpegArgs, EmptyIsNoArgs) {
	EXPECT_TRUE(encoding::ffmpeg_string_to_args("").empty());
	EXPECT_TRUE(encoding::ffmpeg_string_to_args("   ").empty());
}
