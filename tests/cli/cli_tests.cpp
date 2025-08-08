#include "cli/cli.h"

const std::filesystem::path CURRENT_DIR = std::filesystem::path(__FILE__).parent_path();
const std::filesystem::path TEST_OUTPUT_DIR = CURRENT_DIR / "test_outputs";

namespace test_utils {
	const std::filesystem::path TEST_VIDEO_PATH = CURRENT_DIR / "../assets/test_video.mp4";

	bool copy_test_video(const std::filesystem::path& path) {
		try {
			std::filesystem::copy_file(TEST_VIDEO_PATH, path, std::filesystem::copy_options::overwrite_existing);
			return true;
		}
		catch (...) {
			return false;
		}
	}

	bool create_empty_config_file(const std::filesystem::path& path) {
		try {
			std::ofstream file(path);
			return true;
		}
		catch (...) {
			return false;
		}
	}

	bool create_invalid_video_file(const std::filesystem::path& path) {
		try {
			std::ofstream file(path);
			file << "This is not a valid video file";
			return true;
		}
		catch (...) {
			return false;
		}
	}

	bool create_nested_output_path(const std::filesystem::path& base_dir, std::filesystem::path& output_path) {
		output_path = base_dir / "nested" / "directories" / "output.mp4";
		return true;
	}

	// Creates a standard test environment with video and config files
	std::tuple<std::filesystem::path, std::filesystem::path> create_test_files(const std::filesystem::path& dir) {
		auto video = dir / "test.mp4";
		auto config = dir / "config.cfg";
		copy_test_video(video);
		create_empty_config_file(config);
		return { video, config };
	}

	std::vector<std::filesystem::path> generate_output_paths(const std::filesystem::path& dir, size_t count) {
		std::vector<std::filesystem::path> outputs;
		outputs.reserve(count);
		for (size_t i = 0; i < count; ++i) {
			outputs.push_back(dir / ("output" + std::to_string(i + 1) + ".mp4"));
		}
		return outputs;
	}
}

// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes) gtest needs it i think
class CLITest : public ::testing::Test {
protected:
	std::filesystem::path m_test_dir;
	std::filesystem::path m_test_video;
	std::filesystem::path m_test_config;

	void SetUp() override {
		m_test_dir = TEST_OUTPUT_DIR / ::testing::UnitTest::GetInstance()->current_test_info()->name();
		// std::filesystem::remove_all(m_test_dir);
		std::filesystem::create_directories(m_test_dir);

		m_test_video = m_test_dir / "test.mp4";
		m_test_config = m_test_dir / "config.cfg";
		test_utils::copy_test_video(m_test_video);
		test_utils::create_empty_config_file(m_test_config);
	}

	void TearDown() override {
		std::filesystem::remove_all(m_test_dir);
	}
};

// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

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

	EXPECT_TRUE(std::filesystem::exists(m_test_video.parent_path() / (m_test_video.stem().string() + " - blur.mp4")));
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

// TODO: add tests for things that i've had to fix
// e.g. variable framerate output length, configs with invalid values printing the right errors, etc.
