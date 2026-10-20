#include "helpers.h"

#include "common/rendering/render.h"

namespace {
	using Frames = std::pair<size_t, size_t>;

	// 60fps, starting video_offset seconds into a container that's duration long
	media::VideoInfo video_info(double duration, double video_offset = 0.0, double video_duration = -1.0) {
		media::VideoInfo info;
		info.has_video_stream = true;
		info.fps_num = 60;
		info.fps_den = 1;
		info.duration = static_cast<float>(duration);
		info.video_start_time = video_offset;
		info.video_duration = video_duration >= 0.0 ? video_duration : duration - video_offset;
		return info;
	}
}

TEST(TrimFrames, MapsTheTimelineToFrames) {
	auto info = video_info(10.0);

	EXPECT_EQ(rendering::get_trim_frame_range(info, 0.f, 1.f), Frames(0, 600));
	EXPECT_EQ(rendering::get_trim_frame_range(info, 0.1f, 0.5f), Frames(60, 300));
}

TEST(TrimFrames, CountsFromWhereTheVideoStarts) {
	// the first 2s are audio only
	auto info = video_info(10.0, 2.0);

	EXPECT_EQ(rendering::get_trim_frame_range(info, 0.f, 0.5f), Frames(0, 180));
	EXPECT_EQ(rendering::get_trim_frame_range(info, 0.3f, 1.f), Frames(60, 480));
}

TEST(TrimFrames, NothingWithoutAFramerate) {
	EXPECT_EQ(rendering::get_trim_frame_range(media::VideoInfo{}, 0.f, 1.f), Frames(0, 0));
}

TEST(TrimFrames, NeedsVideoInTheTrim) {
	// video from 2s to 8s of 10s
	auto info = video_info(10.0, 2.0, 6.0);

	EXPECT_TRUE(rendering::has_enough_frames_to_render(info, 0.f, 1.f));
	EXPECT_TRUE(rendering::has_enough_frames_to_render(info, 0.15f, 0.25f));
	EXPECT_FALSE(rendering::has_enough_frames_to_render(info, 0.5f, 0.5f));
	EXPECT_FALSE(rendering::has_enough_frames_to_render(info, 0.f, 0.1f));
	EXPECT_FALSE(rendering::has_enough_frames_to_render(info, 0.85f, 1.f));
}
