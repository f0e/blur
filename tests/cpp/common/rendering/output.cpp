#include "helpers.h"

#include "common/rendering/output.h"

class OutputFilename : public TempDirTest {};

TEST_F(OutputFilename, GoesNextToTheInput) {
	BlurSettings settings;

	EXPECT_EQ(rendering::build_output_filename(m_test_dir / "clip.mp4", settings, {}), m_test_dir / "clip - blur.mp4");
}

TEST_F(OutputFilename, NumbersRatherThanOverwriting) {
	BlurSettings settings;
	std::ofstream(m_test_dir / "clip - blur.mp4");
	std::ofstream(m_test_dir / "clip - blur (2).mp4");

	EXPECT_EQ(
		rendering::build_output_filename(m_test_dir / "clip.mp4", settings, {}), m_test_dir / "clip - blur (3).mp4"
	);
}

TEST_F(OutputFilename, UsesTheOutputPrefixAndContainer) {
	BlurSettings settings;
	settings.advanced.video_container = "mkv";

	GlobalAppSettings app_settings;
	app_settings.output_prefix = "blurred";

	auto result = rendering::build_output_filename(m_test_dir / "clip.mp4", settings, app_settings);

	EXPECT_EQ(result, m_test_dir / "blurred" / "clip - blur.mkv");
	EXPECT_TRUE(std::filesystem::is_directory(m_test_dir / "blurred"));
}

TEST_F(OutputFilename, DetailedNamesDescribeTheRender) {
	BlurSettings settings;
	settings.detailed_filenames = true;
	settings.blur_output_fps = 60;
	settings.interpolated_fps = "1200";
	settings.blur_amount = 1.5f;

	auto input = m_test_dir / "clip.mp4";
	EXPECT_EQ(
		rendering::build_output_filename(input, settings, {}), m_test_dir / "clip - blur ~ 60fps (1200, 1.5).mp4"
	);

	settings.interpolate = false;
	EXPECT_EQ(rendering::build_output_filename(input, settings, {}), m_test_dir / "clip - blur ~ 60fps (1.5).mp4");

	settings.blur = false;
	settings.interpolate = true;
	EXPECT_EQ(rendering::build_output_filename(input, settings, {}), m_test_dir / "clip - blur ~ 1200fps.mp4");

	settings.interpolate = false;
	EXPECT_EQ(rendering::build_output_filename(input, settings, {}), m_test_dir / "clip - blur.mp4");
}

TEST(AudioCopy, FindsCopyFlags) {
	using Args = std::vector<std::string>;

	EXPECT_TRUE(rendering::wants_audio_copy(Args{ "-c:v", "libx264", "-c:a", "copy" }));
	EXPECT_TRUE(rendering::wants_audio_copy(Args{ "-acodec", "copy" }));
	EXPECT_TRUE(rendering::wants_audio_copy(Args{ "-codec:a:0", "copy" }));
	EXPECT_FALSE(rendering::wants_audio_copy(Args{ "-c:v", "copy", "-c:a", "aac" }));
	EXPECT_FALSE(rendering::wants_audio_copy(Args{ "-c:a" }));
}

TEST(AudioCopy, ConflictsWithTrimmingAndTimescale) {
	BlurSettings settings;
	EXPECT_FALSE(rendering::get_audio_copy_conflict(settings, false));
	EXPECT_TRUE(rendering::get_audio_copy_conflict(settings, true));

	// on, but not changing the speed
	settings.timescale = true;
	EXPECT_FALSE(rendering::get_audio_copy_conflict(settings, false));

	settings.output_timescale = 0.5f;
	EXPECT_TRUE(rendering::get_audio_copy_conflict(settings, false));
}

TEST(SkippedFrames, SkipsTheFramesWithoutAFullBlur) {
	BlurSettings settings;
	GlobalAppSettings app_settings;
	media::VideoInfo video_info;

	settings.blur_amount = 1.f;
	EXPECT_EQ(rendering::get_skipped_frames(settings, app_settings, video_info), 1);

	settings.blur_amount = 3.f;
	EXPECT_EQ(rendering::get_skipped_frames(settings, app_settings, video_info), 2);

	app_settings.fully_blur_first_frame = false;
	EXPECT_EQ(rendering::get_skipped_frames(settings, app_settings, video_info), 0);

	app_settings.fully_blur_first_frame = true;
	settings.blur = false;
	EXPECT_EQ(rendering::get_skipped_frames(settings, app_settings, video_info), 0);
}

TEST(SkippedFrames, NoneWhenCopiedAudioCantBeTrimmedToMatch) {
	BlurSettings settings;
	settings.advanced.ffmpeg_override = "-c:v libx264 -c:a copy";

	// no audio to copy
	media::VideoInfo video_info;
	EXPECT_EQ(rendering::get_skipped_frames(settings, {}, video_info), 1);

	video_info.audio_sample_rates = { 48000 };
	EXPECT_EQ(rendering::get_skipped_frames(settings, {}, video_info), 0);
}
