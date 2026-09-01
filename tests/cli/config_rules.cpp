#include "tests.h"

#include "common/config_rules.h"

#include "common/config_blur.h"

namespace {
	const std::vector<std::string> AVAILABLE = { "apex", "valorant" };

	ConfigRuleSettings make_settings(std::vector<ConfigRule> rules) {
		return ConfigRuleSettings{ .rules = std::move(rules) };
	}
}

TEST(ConfigRules, FirstMatchWins) {
	auto settings = make_settings(
		{
			{ .pattern = "*clips*", .config_name = "apex" },
			{ .pattern = "*valorant*", .config_name = "valorant" },
		}
	);

	EXPECT_EQ(config_rules::match(settings, "D:/clips/valorant/round.mp4", AVAILABLE), "apex");

	std::ranges::reverse(settings.rules);

	EXPECT_EQ(config_rules::match(settings, "D:/clips/valorant/round.mp4", AVAILABLE), "valorant");
}

TEST(ConfigRules, SkipsRulesForMissingConfigs) {
	auto settings = make_settings(
		{
			{ .pattern = "*clips*", .config_name = "deleted" },
			{ .pattern = "*valorant*", .config_name = "valorant" },
		}
	);

	EXPECT_EQ(config_rules::match(settings, "D:/clips/valorant/round.mp4", AVAILABLE), "valorant");
}

TEST(ConfigRules, SkipsIncompleteRules) {
	auto settings = make_settings(
		{
			{ .pattern = "", .config_name = "apex" },
			{ .pattern = "*clips*", .config_name = "" },
		}
	);

	EXPECT_TRUE(config_rules::match(settings, "D:/clips/round.mp4", AVAILABLE).empty());
	EXPECT_FALSE(config_rules::any_usable(settings, AVAILABLE));
}

TEST(ConfigRules, NoMatchIsEmpty) {
	auto settings = make_settings(
		{
			{ .pattern = "*apex*", .config_name = "apex" },
		}
	);

	EXPECT_TRUE(config_rules::match(settings, "D:/clips/valorant/round.mp4", AVAILABLE).empty());
	EXPECT_TRUE(config_rules::match({}, "D:/clips/round.mp4", AVAILABLE).empty());
}

TEST(ConfigRules, AnyUsable) {
	EXPECT_FALSE(config_rules::any_usable({}, AVAILABLE));

	EXPECT_TRUE(
		config_rules::any_usable(
			make_settings(
				{
					{ .pattern = "*apex*", .config_name = "apex" },
				}
			),
			AVAILABLE
		)
	);
}

TEST(ConfigRules, RoundTripsThroughItsFileFormat) {
	auto settings = make_settings(
		{
			{ .pattern = "D:/clips/apex/*", .config_name = "apex" },
			{ .pattern = "*valorant*", .config_name = "valorant" },
		}
	);

	EXPECT_EQ(config_rules::parse(config_rules::generate_config_string(settings)), settings);
}

TEST(ConfigRules, IgnoresLegacyEnabledSetting) {
	auto settings = config_rules::parse(std::string("- rule\nconfig: apex\npattern: *clips*\nenabled: false\n"));

	EXPECT_EQ(settings, make_settings({ { .pattern = "*clips*", .config_name = "apex" } }));
	EXPECT_EQ(config_rules::match(settings, "D:/clips/round.mp4", AVAILABLE), "apex");
	EXPECT_EQ(config_rules::generate_config_string(settings).find("enabled:"), std::string::npos);
}

TEST(ConfigRules, RoundTripsAnEmptyList) {
	EXPECT_EQ(config_rules::parse(config_rules::generate_config_string({})), ConfigRuleSettings{});
}

TEST(ConfigRules, RenameConfigRepointsMatchingRulesOnly) {
	auto settings = make_settings(
		{
			{ .pattern = "*apex*", .config_name = "apex" },
			{ .pattern = "*valorant*", .config_name = "valorant" },
			{ .pattern = "*r5*", .config_name = "apex" },
		}
	);

	settings.default_config = "apex";

	config_rules::rename_config(settings, "apex", "apex legends");

	EXPECT_EQ(settings.rules[0].config_name, "apex legends");
	EXPECT_EQ(settings.rules[1].config_name, "valorant");
	EXPECT_EQ(settings.rules[2].config_name, "apex legends");
	EXPECT_EQ(settings.default_config, "apex legends");
}

TEST(ConfigRules, RoundTripsTheDefaultConfig) {
	auto settings = make_settings({ { .pattern = "*apex*", .config_name = "apex" } });
	settings.default_config = "valorant";

	EXPECT_EQ(config_rules::parse(config_rules::generate_config_string(settings)), settings);

	settings.default_config.clear();

	EXPECT_EQ(config_rules::parse(config_rules::generate_config_string(settings)), settings);
}

// the wiring both the gui and the cli go through, rather than the matching on its own
class ConfigRuleResolution : public ::testing::Test {
protected:
	std::filesystem::path m_settings_dir;
	std::filesystem::path m_old_settings_path;

	void SetUp() override {
		m_old_settings_path = blur.settings_path;

		m_settings_dir = TEST_OUTPUT_DIR / ::testing::UnitTest::GetInstance()->current_test_info()->name();
		std::filesystem::remove_all(m_settings_dir);
		std::filesystem::create_directories(m_settings_dir);

		blur.settings_path = m_settings_dir;

		config_blur::save("apex", config_blur::DEFAULT_CONFIG);
		config_blur::save("valorant", config_blur::DEFAULT_CONFIG);
	}

	void TearDown() override {
		blur.settings_path = m_old_settings_path;
		std::filesystem::remove_all(m_settings_dir);
	}
};

TEST_F(ConfigRuleResolution, RulePicksTheConfig) {
	config_rules::save(
		ConfigRuleSettings{
			.rules = { { .pattern = "*valorant*", .config_name = "valorant" } },
			.default_config = "apex",
		}
	);

	auto matched = config_blur::resolve_config("D:/clips/valorant/round.mp4", {});
	EXPECT_EQ(matched.name, "valorant");
	EXPECT_EQ(matched.source, config_blur::ConfigSource::RULE);
	EXPECT_EQ(matched.rule_pattern, "*valorant*");

	auto fell_through = config_blur::resolve_config("D:/clips/other/round.mp4", {});
	EXPECT_EQ(fell_through.name, "apex");
	EXPECT_EQ(fell_through.source, config_blur::ConfigSource::DEFAULT);
	EXPECT_TRUE(fell_through.rule_pattern.empty());
}

TEST_F(ConfigRuleResolution, AnExplicitChoiceBeatsARule) {
	config_rules::save(
		ConfigRuleSettings{
			.rules = { { .pattern = "*valorant*", .config_name = "valorant" } },
		}
	);

	EXPECT_EQ(config_blur::resolve_config_name("D:/clips/valorant/round.mp4", "apex"), "apex");
}

TEST_F(ConfigRuleResolution, ARuleForADeletedConfigFallsThroughToTheDefault) {
	config_rules::save(
		ConfigRuleSettings{
			.rules = { { .pattern = "*valorant*", .config_name = "valorant" } },
			.default_config = "apex",
		}
	);

	config_blur::remove("valorant");

	EXPECT_EQ(config_blur::resolve_config_name("D:/clips/valorant/round.mp4", {}), "apex");
}

TEST_F(ConfigRuleResolution, NothingResolvesWithNoDefaultAndNoMatch) {
	config_rules::save(ConfigRuleSettings{ .default_config = "" });

	EXPECT_TRUE(config_blur::resolve_config_name("D:/clips/round.mp4", {}).empty());
}
