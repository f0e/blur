#pragma once

class CLITest : public ::testing::Test {
protected:
	std::filesystem::path m_test_dir;
	std::filesystem::path m_test_video;
	std::filesystem::path m_test_config;

	void SetUp() override;

	void TearDown() override;
};
