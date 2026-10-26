#include "helpers.h"

#include "cli/cli.h"

namespace {
	void create_invalid_video_file(const std::filesystem::path& path) {
		std::ofstream(path) << "This is not a valid video file";
	}

	bool any_exist(const std::vector<std::filesystem::path>& paths) {
		return std::ranges::any_of(paths, [](const auto& path) {
			return std::filesystem::exists(path);
		});
	}
}

// these render

TEST_F(CLITest, SingleFileWithConfig) {
	std::vector<std::filesystem::path> inputs{ copy_test_clip() };
	std::vector<std::filesystem::path> configs{ m_test_config };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_TRUE(cli::run(inputs, outputs, configs, false, true, true));

	EXPECT_TRUE(std::filesystem::exists(outputs[0]));
}

TEST_F(CLITest, DefaultConfigAndOutputName) {
	auto input = copy_test_clip();

	EXPECT_TRUE(cli::run({ input }, {}, {}, false, true, true));

	EXPECT_TRUE(std::filesystem::exists(input.parent_path() / std::format("{} - blur.mp4", input.stem())));
}

TEST_F(CLITest, MixedSuccessAndFailure) {
	auto video = copy_test_clip();
	auto invalid_video = m_test_dir / "invalid.mp4";
	create_invalid_video_file(invalid_video);

	std::vector<std::filesystem::path> inputs{
		video,
		invalid_video,
		m_test_dir / "nonexistent.mp4",
		video,
	};
	std::vector<std::filesystem::path> configs(inputs.size(), m_test_config);
	auto outputs = test_utils::generate_output_paths(m_test_dir, inputs.size());

	EXPECT_FALSE(cli::run(inputs, outputs, configs, false, true, true));

	EXPECT_TRUE(std::filesystem::exists(outputs[0]));
	EXPECT_FALSE(std::filesystem::exists(outputs[1]));
	EXPECT_FALSE(std::filesystem::exists(outputs[2]));
	EXPECT_TRUE(std::filesystem::exists(outputs[3]));
}

TEST_F(CLITest, OutputInNonExistentDirectory) {
	auto output = m_test_dir / "nested" / "directories" / "output.mp4";

	EXPECT_TRUE(cli::run({ copy_test_clip() }, { output }, {}, false, true, true));

	EXPECT_TRUE(std::filesystem::exists(output));
}

TEST_F(CLITest, OutputWithNoDirectory) {
	auto input = copy_test_clip();

	auto old_cwd = std::filesystem::current_path();
	std::filesystem::current_path(m_test_dir);
	bool res = cli::run({ input }, { "output.mp4" }, {}, false, true, true);
	std::filesystem::current_path(old_cwd);

	EXPECT_TRUE(res);
	EXPECT_TRUE(std::filesystem::exists(m_test_dir / "output.mp4"));
}

TEST_F(CLITest, ConfigNameBeatsConfigPath) {
	// told apart by the container they pick for the output name
	auto mkv = test_utils::fast_config();
	mkv.override_advanced = true;
	mkv.advanced.video_container = "mkv";
	config_blur::save("mkv", mkv);

	auto input = copy_test_clip();

	EXPECT_TRUE(cli::run({ input }, {}, { m_test_config }, false, true, true, "", {}, { "mkv" }));

	EXPECT_TRUE(std::filesystem::exists(m_test_dir / "test - blur.mkv"));
	EXPECT_FALSE(std::filesystem::exists(m_test_dir / "test - blur.mp4"));
}

// these should fail before anything renders

TEST_F(CLITest, InputOutputCountMismatch) {
	auto video = copy_test_clip();
	std::vector<std::filesystem::path> inputs{ video, video };
	std::vector<std::filesystem::path> configs{ m_test_config, m_test_config };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_FALSE(cli::run(inputs, outputs, configs, false, true, true));

	EXPECT_FALSE(any_exist(outputs));
}

TEST_F(CLITest, ConfigCountMismatch) {
	auto video = copy_test_clip();
	std::vector<std::filesystem::path> inputs{ video, video };
	std::vector<std::filesystem::path> configs{ m_test_config };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 2);

	EXPECT_FALSE(cli::run(inputs, outputs, configs, false, true, true));

	EXPECT_FALSE(any_exist(outputs));
}

TEST_F(CLITest, NonExistentConfigFile) {
	std::vector<std::filesystem::path> configs{ m_test_dir / "nonexistent.cfg" };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_FALSE(cli::run({ copy_test_clip() }, outputs, configs, false, true, true));

	EXPECT_FALSE(any_exist(outputs));
}

TEST_F(CLITest, ConfigNameCountMismatch) {
	auto video = copy_test_clip();
	auto outputs = test_utils::generate_output_paths(m_test_dir, 2);

	EXPECT_FALSE(cli::run({ video, video }, outputs, {}, false, true, true, "", {}, { "default" }));

	EXPECT_FALSE(any_exist(outputs));
}

TEST_F(CLITest, UnknownConfigName) {
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_FALSE(cli::run({ copy_test_clip() }, outputs, {}, false, true, true, "", {}, { "nonexistent" }));

	EXPECT_FALSE(any_exist(outputs));
}

TEST_F(CLITest, NonExistentInputFile) {
	std::vector<std::filesystem::path> configs{ m_test_config };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_FALSE(cli::run({ m_test_dir / "nonexistent.mp4" }, outputs, configs, false, true, true));

	EXPECT_FALSE(any_exist(outputs));
}

TEST_F(CLITest, InvalidVideoFile) {
	auto invalid_video = m_test_dir / "invalid.mp4";
	create_invalid_video_file(invalid_video);
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_FALSE(cli::run({ invalid_video }, outputs, {}, false, true, true));

	EXPECT_FALSE(any_exist(outputs));
}

TEST_F(CLITest, TrimWithNoFrames) {
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_FALSE(cli::run({ copy_test_clip() }, outputs, {}, false, true, true, "", {}, {}, 0.2, 0.2));

	EXPECT_FALSE(any_exist(outputs));
}

// TODO: add tests for things that i've had to fix
// e.g. variable framerate output length, configs with invalid values printing the right errors, etc.
