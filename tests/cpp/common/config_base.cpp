#include "helpers.h"

#include "common/config_base.h"

class ConfigMigration : public TempDirTest {
protected:
	std::filesystem::path m_from;
	std::filesystem::path m_to;

	void SetUp() override {
		TempDirTest::SetUp();
		m_from = m_test_dir / "old.cfg";
		m_to = m_test_dir / "new.cfg";
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

TEST(ConfigMap, SplitsOnTheFirstColon) {
	std::istringstream stream("custom ffmpeg filters: -vf scale=1280:-2\nmask: C:/masks/hud.png\n");

	auto config = config_base::read_config_map(stream);

	EXPECT_EQ(config.at("custom ffmpeg filters"), "-vf scale=1280:-2");
	EXPECT_EQ(config.at("mask"), "C:/masks/hud.png");
}

TEST(ConfigMap, SkipsLinesThatArentSettings) {
	std::istringstream stream("[blur v3.0.0]\n\n- blur\n: no key\n  blur amount :  2 \r\n");

	const config_base::ConfigMap expected{ { "blur amount", "2" } };
	EXPECT_EQ(config_base::read_config_map(stream), expected);
}

TEST(ConfigMap, LaterLinesWin) {
	std::istringstream stream("quality: 16\nquality: 20\n");

	EXPECT_EQ(config_base::read_config_map(stream).at("quality"), "20");
}

TEST(ConfigVersion, OnlyTheFirstLineIsTheHeader) {
	EXPECT_EQ(config_base::parse_config_version("\n\n[blur v3.1.0]\nblur: true\n"), "3.1.0");
	EXPECT_FALSE(config_base::parse_config_version("blur: true\n[blur v3.1.0]\n").has_value());
	EXPECT_FALSE(config_base::parse_config_version("").has_value());
}

TEST(ConfigValues, ParsesEachType) {
	const config_base::ConfigMap config{ { "flag", "true" }, { "number", "-3" }, { "decimal", "0.25" } };

	bool flag = false;
	int number = 0;
	float decimal = 0.f;
	config_base::extract_config_value(config, "flag", flag);
	config_base::extract_config_value(config, "number", number);
	config_base::extract_config_value(config, "decimal", decimal);

	EXPECT_TRUE(flag);
	EXPECT_EQ(number, -3);
	EXPECT_EQ(decimal, 0.25f);
}

TEST(ConfigValues, InvalidValuesKeepTheDefault) {
	const config_base::ConfigMap config{ { "flag", "yes" }, { "number", "sixty" }, { "decimal", "" } };

	bool flag = true;
	int number = 60;
	float decimal = 1.5f;
	config_base::extract_config_value(config, "flag", flag);
	config_base::extract_config_value(config, "number", number);
	config_base::extract_config_value(config, "decimal", decimal);

	EXPECT_TRUE(flag);
	EXPECT_EQ(number, 60);
	EXPECT_EQ(decimal, 1.5f);
}

TEST(ConfigValues, MissingValuesKeepTheDefault) {
	int number = 60;
	config_base::extract_config_value({}, "number", number);

	EXPECT_EQ(number, 60);
}
