#include "tests.h"

#include "cli/cli.h"

TEST_F(CLITest, SingleFileWithConfig) {
	std::vector<std::filesystem::path> inputs{ m_test_video };
	std::vector<std::filesystem::path> configs{ m_test_config };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_TRUE(cli::run(inputs, outputs, configs, false, true, true));

	for (const auto& output : outputs) {
		EXPECT_TRUE(std::filesystem::exists(output));
	}
}

TEST_F(CLITest, SingleFileNoConfig) {
	std::vector<std::filesystem::path> inputs{ m_test_video };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_TRUE(cli::run(inputs, outputs, {}, false, true, true));

	for (const auto& output : outputs) {
		EXPECT_TRUE(std::filesystem::exists(output));
	}
}

TEST_F(CLITest, SingleFileNoConfigNoOutput) {
	std::vector<std::filesystem::path> inputs{ m_test_video };

	EXPECT_TRUE(cli::run(inputs, {}, {}, false, true, true));

	EXPECT_TRUE(
		std::filesystem::exists(m_test_video.parent_path() / (std::format("{} - blur.mp4", m_test_video.stem())))
	);
}

TEST_F(CLITest, InputOutputCountMismatch) {
	std::vector<std::filesystem::path> inputs{ m_test_video, m_test_video };
	std::vector<std::filesystem::path> configs{ m_test_config, m_test_config };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_FALSE(cli::run(inputs, outputs, configs, false, true, true));

	for (const auto& output : outputs) {
		EXPECT_FALSE(std::filesystem::exists(output));
	}
}

TEST_F(CLITest, ConfigCountMismatch) {
	std::vector<std::filesystem::path> inputs{ m_test_video, m_test_video };
	std::vector<std::filesystem::path> configs{ m_test_config };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 2);

	EXPECT_FALSE(cli::run(inputs, outputs, configs, false, true, true));

	for (const auto& output : outputs) {
		EXPECT_FALSE(std::filesystem::exists(output));
	}
}

TEST_F(CLITest, NonExistentInputFile) {
	std::vector<std::filesystem::path> inputs{ m_test_dir / "nonexistent.mp4" };
	std::vector<std::filesystem::path> configs{ m_test_config };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_TRUE(cli::run(inputs, outputs, configs, false, true, true));

	for (const auto& output : outputs) {
		EXPECT_FALSE(std::filesystem::exists(output));
	}
}

TEST_F(CLITest, NonExistentConfigFile) {
	std::vector<std::filesystem::path> inputs{ m_test_video };
	std::vector<std::filesystem::path> configs{ m_test_dir / "nonexistent.cfg" };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_FALSE(cli::run(inputs, outputs, configs, false, true, true));

	for (const auto& output : outputs) {
		EXPECT_FALSE(std::filesystem::exists(output));
	}
}

TEST_F(CLITest, MultipleValidInputsWithOutputs) {
	std::vector<std::filesystem::path> inputs{ m_test_video, m_test_video };
	std::vector<std::filesystem::path> configs{ m_test_config, m_test_config };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 2);

	EXPECT_TRUE(cli::run(inputs, outputs, configs, false, true, true));

	for (const auto& output : outputs) {
		EXPECT_TRUE(std::filesystem::exists(output));
	}
}

TEST_F(CLITest, MultipleInputsWithConfigs) {
	std::vector<std::filesystem::path> inputs{ m_test_video, m_test_video };
	std::vector<std::filesystem::path> configs{ m_test_config, m_test_config };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 2);

	EXPECT_TRUE(cli::run(inputs, outputs, configs, false, true, true));

	for (const auto& output : outputs) {
		EXPECT_TRUE(std::filesystem::exists(output));
	}
}

TEST_F(CLITest, InvalidVideoFile) {
	auto invalid_video = m_test_dir / "invalid.mp4";
	test_utils::create_invalid_video_file(invalid_video);

	std::vector<std::filesystem::path> inputs{ invalid_video };
	auto outputs = test_utils::generate_output_paths(m_test_dir, 1);

	EXPECT_TRUE(cli::run(inputs, outputs, {}, false, true, true));

	for (const auto& output : outputs) {
		EXPECT_FALSE(std::filesystem::exists(output));
	}
}

TEST_F(CLITest, OutputInNonExistentDirectory) {
	std::vector<std::filesystem::path> inputs{ m_test_video };
	std::filesystem::path nested_output;
	test_utils::create_nested_output_path(m_test_dir, nested_output);
	std::vector<std::filesystem::path> outputs{ nested_output };

	EXPECT_TRUE(cli::run(inputs, outputs, {}, false, true, true));

	for (const auto& output : outputs) {
		EXPECT_TRUE(std::filesystem::exists(output));
	}
}

TEST_F(CLITest, MixedSuccessAndFailure) {
	auto invalid_video = m_test_dir / "invalid.mp4";
	test_utils::create_invalid_video_file(invalid_video);

	std::vector<std::filesystem::path> inputs{
		m_test_video,
		invalid_video,
		m_test_dir / "nonexistent.mp4",
		m_test_video,
	};
	auto outputs = test_utils::generate_output_paths(m_test_dir, 4);

	EXPECT_TRUE(cli::run(inputs, outputs, {}, false, true, true));

	EXPECT_TRUE(std::filesystem::exists(outputs[0]));  // valid video should succeed
	EXPECT_FALSE(std::filesystem::exists(outputs[1])); // invalid video should fail
	EXPECT_FALSE(std::filesystem::exists(outputs[2])); // nonexistent video should fail
	EXPECT_TRUE(std::filesystem::exists(outputs[3]));  // second valid video should succeed
}
