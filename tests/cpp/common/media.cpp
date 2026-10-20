#include "helpers.h"

#include "common/media.h"

// the clip is made with blur, which needs settings to initialise
class ProbeVideo : public SettingsTest {};

TEST_F(ProbeVideo, ReadsTheTestClip) {
	auto info = media::get_video_info(test_utils::test_clip());

	EXPECT_TRUE(info.has_video_stream);
	EXPECT_EQ(info.width, 160);
	EXPECT_EQ(info.height, 90);
	EXPECT_EQ(info.fps_num, 30);
	EXPECT_EQ(info.fps_den, 1);
	EXPECT_NEAR(info.video_duration, 0.5, 0.05);
	EXPECT_EQ(info.audio_sample_rates.size(), 1);
}

TEST_F(ProbeVideo, NoVideoInAnInvalidFile) {
	auto path = m_test_dir / "invalid.mp4";
	std::ofstream(path) << "not a video";

	EXPECT_FALSE(media::get_video_info(path).has_video_stream);
}

TEST_F(ProbeVideo, NoVideoInAMissingFile) {
	EXPECT_FALSE(media::get_video_info(m_test_dir / "missing.mp4").has_video_stream);
}
