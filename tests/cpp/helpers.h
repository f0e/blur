#pragma once

#include "common/config_blur.h"

const std::filesystem::path REPO_DIR = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();

namespace test_utils {
	// this process's own folder, so parallel runs (ctest -j) don't share files
	const std::filesystem::path& output_dir();

	// a short low res clip with audio, made with blur's ffmpeg the first time it's asked for
	const std::filesystem::path& test_clip();

	// a config that only decodes and encodes, so renders take as little time as possible
	BlurSettings fast_config();

	std::vector<std::filesystem::path> generate_output_paths(const std::filesystem::path& dir, size_t count);
}

// a fresh folder for each test, removed after it
class TempDirTest : public ::testing::Test {
protected:
	std::filesystem::path m_test_dir;

	void SetUp() override;
	void TearDown() override;
};

// blur's settings live in the test's folder
class SettingsTest : public TempDirTest {
protected:
	void SetUp() override;
};

class CLITest : public SettingsTest {
protected:
	std::filesystem::path m_test_config;

	void SetUp() override;

	// copied into the test's folder, so anything written next to it is cleaned up with it
	std::filesystem::path copy_test_clip(const std::string& name = "test.mp4");
};
