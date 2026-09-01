#include "tests.h"

#include "common/config_base.h"

class ConfigMigration : public ::testing::Test {
protected:
	std::filesystem::path m_test_dir;
	std::filesystem::path m_from;
	std::filesystem::path m_to;

	void SetUp() override {
		m_test_dir = TEST_OUTPUT_DIR / ::testing::UnitTest::GetInstance()->current_test_info()->name();
		std::filesystem::remove_all(m_test_dir);
		std::filesystem::create_directories(m_test_dir);
		m_from = m_test_dir / "old.cfg";
		m_to = m_test_dir / "new.cfg";
	}

	void TearDown() override {
		std::filesystem::remove_all(m_test_dir);
	}
};

TEST_F(ConfigMigration, RenamesOriginalWhenDestinationIsMissing) {
	config_base::write_config_string(m_from, "old config");

	config_base::migrate_file(m_from, m_to);

	EXPECT_FALSE(std::filesystem::exists(m_from));
	EXPECT_EQ(config_base::read_config_file(m_to), "old config");
}

TEST_F(ConfigMigration, RemovesOriginalWhenDestinationExists) {
	config_base::write_config_string(m_from, "old config");
	config_base::write_config_string(m_to, "new config");

	config_base::migrate_file(m_from, m_to);

	EXPECT_FALSE(std::filesystem::exists(m_from));
	EXPECT_EQ(config_base::read_config_file(m_to), "new config");
}
