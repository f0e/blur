#include "tests.h"

#include "common/config_rules.h"

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

TEST(ConfigRules, SkipsDisabledRules) {
	auto settings = make_settings(
		{
			{ .pattern = "*clips*", .config_name = "apex", .enabled = false },
			{ .pattern = "*valorant*", .config_name = "valorant" },
		}
	);

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

	EXPECT_FALSE(
		config_rules::any_usable(
			make_settings(
				{
					{ .pattern = "*apex*", .config_name = "apex", .enabled = false },
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
			{ .pattern = "*valorant*", .config_name = "valorant", .enabled = false },
		}
	);

	EXPECT_EQ(config_rules::parse(config_rules::generate_config_string(settings)), settings);
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

	config_rules::rename_config(settings, "apex", "apex legends");

	EXPECT_EQ(settings.rules[0].config_name, "apex legends");
	EXPECT_EQ(settings.rules[1].config_name, "valorant");
	EXPECT_EQ(settings.rules[2].config_name, "apex legends");
}
