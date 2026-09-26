#include "tests.h"

#include "cli/cli.h"

namespace test_utils {
	struct FormatTest {
		std::string container;
		std::string codec;
	};

	const std::vector<FormatTest> FORMATS_TO_TEST = {
		{ .container = "mp4", .codec = "libx264" },    { .container = "mp4", .codec = "libx265" },
		{ .container = "mp4", .codec = "h264_nvenc" }, { .container = "mp4", .codec = "hevc_nvenc" },
		{ .container = "webm", .codec = "libvpx" },    { .container = "webm", .codec = "libvpx-vp9" },
		{ .container = "mov", .codec = "libx264" },    { .container = "mov", .codec = "prores" },
		{ .container = "mkv", .codec = "libx264" },    { .container = "mkv", .codec = "libx265" },
		{ .container = "avi", .codec = "libx264" },    { .container = "avi", .codec = "libxvid" },
		{ .container = "flv", .codec = "libx264" },    { .container = "wmv", .codec = "wmv2" },
		{ .container = "3gp", .codec = "libx264" },
	};

	bool make_test_video(const std::filesystem::path& path, const std::string& codec) {
		std::string cmd = std::format(R"(ffmpeg -y -i "{}" -t 2 -c:v {} "{}")", TEST_VIDEO_PATH, codec, path);

		u::log("running {}", cmd);

		return std::system(cmd.c_str()) == 0 && std::filesystem::exists(path);
	}
}

TEST_F(CLITest, AllVideoFormats) {
	std::vector<std::filesystem::path> inputs;
	std::vector<std::filesystem::path> outputs;

	int created = 0;
	for (const auto& format : test_utils::FORMATS_TO_TEST) {
		auto input_path = m_test_dir / (format.container + "_" + format.codec + "." + format.container);

		u::log("creating video file (format: {}, container: {})", format.codec, format.container);

		if (test_utils::make_test_video(input_path, format.codec)) {
			inputs.push_back(input_path);
			outputs.push_back(m_test_dir / ("out_" + std::to_string(created) + ".mp4"));
			created++;
		}
	}

	if (created == 0) {
		GTEST_SKIP() << "No test videos could be created";
	}

	u::log("testing {} different formats", created);

	EXPECT_TRUE(cli::run(inputs, outputs, {}, false, true, true));

	for (const auto& output : outputs) {
		EXPECT_TRUE(std::filesystem::exists(output));
		EXPECT_GT(std::filesystem::file_size(output), 0);
	}
}
