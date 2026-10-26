#include "helpers.h"

#include "common/rendering/render_commands.h"

using rendering::detail::FrameRange;

namespace {
	using Args = std::vector<std::string>;

	const std::filesystem::path INPUT_PATH = "D:/clips/round.mp4";
	const std::filesystem::path OUTPUT_PATH = "D:/clips/round - blur.mp4";

	// 10s at 60fps with one 48khz track
	media::VideoInfo video_info() {
		media::VideoInfo info;
		info.has_video_stream = true;
		info.fps_num = 60;
		info.fps_den = 1;
		info.duration = 10.f;
		info.video_duration = 10.0;
		info.audio_sample_rates = { 48000 };
		return info;
	}

	// an override so the saved encoding presets aren't read, and no blur so no frames are skipped
	BlurSettings settings(const std::string& encoding = "-c:v libx264 -crf 20") {
		BlurSettings settings;
		settings.blur = false;
		settings.advanced.ffmpeg_override = encoding;
		return settings;
	}

	BlurSettings timescaled(float speed, bool pitch) {
		auto timescaled = settings();
		timescaled.timescale = true;
		timescaled.output_timescale = speed;
		timescaled.output_timescale_audio_pitch = pitch;
		return timescaled;
	}

	Args video_args(
		const media::VideoInfo& info, const BlurSettings& settings, FrameRange range = { 0, 600 }, bool trimming = false
	) {
		auto args =
			rendering::detail::build_ffmpeg_video_args(INPUT_PATH, info, settings, {}, OUTPUT_PATH, range, trimming);
		EXPECT_TRUE(args) << args.error();
		return args.value_or(Args{});
	}

	std::optional<std::string> value_after(const Args& args, const std::string& flag) {
		auto it = std::ranges::find(args, flag);
		if (it == args.end() || std::next(it) == args.end())
			return {};
		return *std::next(it);
	}

	bool has_sequence(const Args& args, const Args& sequence) {
		return !std::ranges::search(args, sequence).empty();
	}

	// what each audio track is trimmed to, in samples, and the filters after the trim
	struct AudioTrim {
		int64_t start = 0;
		int64_t end = 0;
		std::string filters;
	};

	std::vector<AudioTrim> audio_trims(const Args& args) {
		static const std::regex TRIM(
			R"(atrim=start_pts=(-?\d+):end_pts=(-?\d+),asetpts=PTS-\(\1\),aresample=first_pts=0([^\[]*)\[a\d+\])"
		);

		std::vector<AudioTrim> trims;
		auto filter = value_after(args, "-filter_complex").value_or("");
		for (std::sregex_iterator it(filter.begin(), filter.end(), TRIM); it != std::sregex_iterator(); ++it) {
			trims.push_back(
				{
					.start = std::stoll((*it)[1]),
					.end = std::stoll((*it)[2]),
					.filters = (*it)[3],
				}
			);
		}
		return trims;
	}

	AudioTrim only_trim(const Args& args) {
		auto trims = audio_trims(args);
		EXPECT_EQ(trims.size(), 1);
		return trims.empty() ? AudioTrim{} : trims[0];
	}
}

TEST(FfmpegAudio, TrimsToTheRenderedFrames) {
	auto whole = only_trim(video_args(video_info(), settings()));
	EXPECT_EQ(whole.start, 0);
	EXPECT_EQ(whole.end, 480000);
	EXPECT_EQ(whole.filters, "");

	auto part = only_trim(video_args(video_info(), settings(), { 60, 120 }));
	EXPECT_EQ(part.start, 48000);
	EXPECT_EQ(part.end, 96000);
}

TEST(FfmpegAudio, CountsFromWhereTheVideoStarts) {
	auto info = video_info();
	info.video_start_time = 0.5;

	auto trim = only_trim(video_args(info, settings(), { 60, 120 }));
	EXPECT_EQ(trim.start, 72000);
	EXPECT_EQ(trim.end, 120000);
}

TEST(FfmpegAudio, SkipsTheAudioOfSkippedFrames) {
	// 3 frames of blur skips the first 2 output frames, at 60fps
	auto blurred = settings();
	blurred.blur = true;
	blurred.blur_amount = 3.f;
	blurred.blur_output_fps = 60;

	EXPECT_EQ(only_trim(video_args(video_info(), blurred)).start, 1600);
}

TEST(FfmpegAudio, TrimsEachTrackAtItsOwnRate) {
	auto info = video_info();
	info.audio_sample_rates = { 48000, 44100 };

	auto args = video_args(info, settings());
	auto trims = audio_trims(args);

	ASSERT_EQ(trims.size(), 2);
	EXPECT_EQ(trims[0].end, 480000);
	EXPECT_EQ(trims[1].end, 441000);
	EXPECT_TRUE(has_sequence(args, { "-map", "[a0]", "-map", "[a1]" }));
}

TEST(FfmpegAudio, NothingWithoutAudio) {
	auto info = video_info();
	info.audio_sample_rates.clear();

	auto args = video_args(info, settings());

	EXPECT_FALSE(value_after(args, "-filter_complex"));
	EXPECT_EQ(std::ranges::count(args, "-map"), 1);
}

TEST(FfmpegAudio, PitchShiftsByResampling) {
	auto trim = only_trim(video_args(video_info(), timescaled(0.5f, true)));

	EXPECT_EQ(trim.start, 0);
	EXPECT_EQ(trim.filters, ",asetrate=24000,aresample=48000");
}

TEST(FfmpegAudio, AtempoGetsItsWindowOfAudioEarly) {
	// atempo starts half its window (sample rate / 24, to a power of two) early
	auto trim = only_trim(video_args(video_info(), timescaled(0.5f, false)));

	EXPECT_EQ(trim.start, -1024);
	EXPECT_EQ(trim.filters, ",atempo=0.500000");
}

TEST(FfmpegAudio, ChainsAtempoPastItsRange) {
	// each stage's window is in the audio the stage before it sees, so the leads add up scaled by the tempo so far
	auto slow = only_trim(video_args(video_info(), timescaled(0.25f, false)));
	EXPECT_EQ(slow.start, -1536);
	EXPECT_EQ(slow.filters, ",atempo=0.500000,atempo=0.500000");

	auto fast = only_trim(video_args(video_info(), timescaled(4.f, false)));
	EXPECT_EQ(fast.start, -3072);
	EXPECT_EQ(fast.filters, ",atempo=2.000000,atempo=2.000000");

	EXPECT_EQ(only_trim(video_args(video_info(), timescaled(0.3f, false))).filters, ",atempo=0.500000,atempo=0.600000");
}

TEST(FfmpegAudio, TimescaleAtTheSameSpeedChangesNothing) {
	auto trim = only_trim(video_args(video_info(), timescaled(1.f, false)));

	EXPECT_EQ(trim.start, 0);
	EXPECT_EQ(trim.filters, "");
}

TEST(FfmpegAudio, CopiedAudioIsMovedToTheVideoStart) {
	auto info = video_info();
	info.video_start_time = 0.5;

	auto args = video_args(info, settings("-c:v libx264 -c:a copy"));

	EXPECT_EQ(value_after(args, "-itsoffset"), "-0.500000");
	EXPECT_TRUE(has_sequence(args, { "-map", "1:a:0" }));
	EXPECT_TRUE(audio_trims(args).empty());
}

TEST(FfmpegAudio, CopiedAudioCantBeTrimmed) {
	auto copying = settings("-c:v libx264 -c:a copy");
	auto build = [&](const media::VideoInfo& info) {
		return rendering::detail::build_ffmpeg_video_args(INPUT_PATH, info, copying, {}, OUTPUT_PATH, { 60, 120 }, true);
	};

	EXPECT_FALSE(build(video_info()));

	// nothing to copy
	auto silent = video_info();
	silent.audio_sample_rates.clear();
	EXPECT_TRUE(build(silent));
}

TEST(FfmpegColour, KeepsTheSourceColours) {
	auto info = video_info();
	info.pix_fmt = "yuv420p10le";
	info.color_range = "pc";
	info.color_space = "bt709";
	info.color_transfer = "bt709";
	info.color_primaries = "bt709";

	auto args = video_args(info, settings());

	EXPECT_EQ(
		value_after(args, "-vf"), "setparams=range=full:colorspace=bt709:color_trc=bt709:color_primaries=bt709"
	);
	EXPECT_EQ(value_after(args, "-pix_fmt"), "yuv420p10le");
}

TEST(FfmpegColour, RgbSourcesDontKeepTheirMatrixOrFormat) {
	// the render converts them to yuv
	auto info = video_info();
	info.pix_fmt = "gbrp";
	info.color_range = "tv";
	info.color_space = "gbr";

	auto args = video_args(info, settings());

	EXPECT_EQ(value_after(args, "-vf"), "setparams=range=limited");
	EXPECT_FALSE(value_after(args, "-pix_fmt"));
}

TEST(FfmpegPreRender, ReencodesTheTrimmedAudio) {
	GlobalAppSettings app_settings;
	app_settings.pre_render_output_encoding = false;

	auto args = rendering::detail::build_ffmpeg_pre_render_args(
					INPUT_PATH, video_info(), settings(), app_settings, OUTPUT_PATH, { 0, 600 }, 0
	)
	                .value();

	EXPECT_TRUE(has_sequence(args, { "-c:a", "aac" }));
	EXPECT_TRUE(u::contains(args, "-shortest"));
	EXPECT_EQ(audio_trims(args).size(), 1);
}

TEST(FfmpegPreRender, UsesTheOutputEncodingButNeverCopiesAudio) {
	GlobalAppSettings app_settings;
	app_settings.pre_render_output_encoding = true;

	auto build = [&](const std::string& encoding) {
		return rendering::detail::build_ffmpeg_pre_render_args(
				   INPUT_PATH, video_info(), settings(encoding), app_settings, OUTPUT_PATH, { 0, 600 }, 0
		)
		    .value();
	};

	auto copying = build("-c:v libx265 -c:a copy");
	EXPECT_TRUE(has_sequence(copying, { "-c:v", "libx265" }));

	// the last one given wins
	auto last_audio_codec = std::find(copying.rbegin(), copying.rend(), "-c:a");
	ASSERT_NE(last_audio_codec, copying.rend());
	EXPECT_EQ(*last_audio_codec.base(), "aac");

	EXPECT_FALSE(u::contains(build("-c:v libx265 -c:a flac"), "aac"));
}

TEST(FfmpegPreRender, LeavesOutAudioFfmpegCantDecode) {
	auto info = video_info();
	info.ffmpeg_can_decode_audio = false;

	auto args =
		rendering::detail::build_ffmpeg_pre_render_args(INPUT_PATH, info, settings(), {}, OUTPUT_PATH, { 0, 600 }, 0)
			.value();

	EXPECT_TRUE(audio_trims(args).empty());
}
