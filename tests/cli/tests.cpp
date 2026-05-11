#include "tests.h"

void CLITest::SetUp() {
	m_test_dir = TEST_OUTPUT_DIR / ::testing::UnitTest::GetInstance()->current_test_info()->name();
	// std::filesystem::remove_all(m_test_dir);
	std::filesystem::create_directories(m_test_dir);

	m_test_video = m_test_dir / "test.mp4";
	m_test_config = m_test_dir / "config.cfg";
	test_utils::copy_test_video(m_test_video);
	test_utils::create_empty_config_file(m_test_config);
}

void CLITest::TearDown() {
	std::filesystem::remove_all(m_test_dir);
}
