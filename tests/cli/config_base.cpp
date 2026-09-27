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

namespace {
	constexpr auto TEST_MIGRATIONS = std::to_array<config_base::Migration>({
		{
			.version = "3.1.0",
			.description = "a -> b",
			.apply =
				[](config_base::ConfigMap& config) {
					auto it = config.find("a");
					if (it == config.end())
						return false;

					config["b"] = it->second;
					config.erase(it);
					return true;
				},
		},
		{
			.version = "3.2.0",
			.description = "b -> c",
			.apply =
				[](config_base::ConfigMap& config) {
					auto it = config.find("b");
					if (it == config.end())
						return false;

					config["c"] = it->second;
					config.erase(it);
					return true;
				},
		},
	});
}

TEST(ConfigVersion, ParsesHeader) {
	EXPECT_EQ(config_base::parse_config_version("[blur v2.45]\n\nblur: true\n"), "2.45");
}

TEST(ConfigVersion, MissingHeaderIsUnversioned) {
	EXPECT_FALSE(config_base::parse_config_version("blur: true\n").has_value());
}

TEST(ConfigVersionMigrations, ChainsThroughEveryMigrationSinceTheConfigWasWritten) {
	config_base::ConfigMap config{ { "a", "1" } };

	config_base::apply_migrations(config, "3.0.0", TEST_MIGRATIONS);

	const config_base::ConfigMap expected{ { "c", "1" } };
	EXPECT_EQ(config, expected);
}

TEST(ConfigVersionMigrations, SkipsMigrationsOlderThanTheConfig) {
	config_base::ConfigMap config{ { "b", "1" } };

	config_base::apply_migrations(config, "3.1.5", TEST_MIGRATIONS);

	const config_base::ConfigMap expected{ { "c", "1" } };
	EXPECT_EQ(config, expected);
}

TEST(ConfigVersionMigrations, RunsNothingForACurrentConfig) {
	config_base::ConfigMap config{ { "c", "1" } };

	config_base::apply_migrations(config, "3.2.0", TEST_MIGRATIONS);

	const config_base::ConfigMap expected{ { "c", "1" } };
	EXPECT_EQ(config, expected);
}

TEST(ConfigVersionMigrations, RunsEverythingWhenTheVersionIsUnknown) {
	config_base::ConfigMap config{ { "a", "1" } };

	config_base::apply_migrations(config, {}, TEST_MIGRATIONS);

	const config_base::ConfigMap expected{ { "c", "1" } };
	EXPECT_EQ(config, expected);
}
