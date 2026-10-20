#include "helpers.h"

#include "common/utils.h"

TEST(PatternMatching, SubstringWhenNoWildcards) {
	EXPECT_TRUE(u::matches_pattern("valorant", "D:/clips/valorant/round.mp4"));
	EXPECT_TRUE(u::matches_pattern("clips", "D:/clips/round.mp4"));
	EXPECT_FALSE(u::matches_pattern("apex", "D:/clips/valorant/round.mp4"));
}

TEST(PatternMatching, Wildcards) {
	EXPECT_TRUE(u::matches_pattern("*valorant*", "D:/clips/valorant/round.mp4"));
	EXPECT_TRUE(u::matches_pattern("D:/clips/apex/*", "D:/clips/apex/round.mp4"));
	EXPECT_FALSE(u::matches_pattern("D:/clips/apex/*", "D:/clips/valorant/round.mp4"));
	EXPECT_TRUE(u::matches_pattern("*.mkv", "D:/clips/round.mkv"));
	EXPECT_FALSE(u::matches_pattern("*.mkv", "D:/clips/round.mp4"));
}

TEST(PatternMatching, WildcardPatternsMatchTheWholePath) {
	EXPECT_FALSE(u::matches_pattern("round?.mp4", "D:/clips/round1.mp4"));
	EXPECT_TRUE(u::matches_pattern("*round?.mp4", "D:/clips/round1.mp4"));
	EXPECT_FALSE(u::matches_pattern("*round?.mp4", "D:/clips/round.mp4"));
	EXPECT_FALSE(u::matches_pattern("*round?.mp4", "D:/clips/round12.mp4"));
}

TEST(PatternMatching, StarCrossesSeparators) {
	EXPECT_TRUE(u::matches_pattern("D:/*.mp4", "D:/clips/valorant/round.mp4"));
}

TEST(PatternMatching, IgnoresCase) {
	EXPECT_TRUE(u::matches_pattern("*VALORANT*", "D:/clips/valorant/round.mp4"));
	EXPECT_TRUE(u::matches_pattern("*valorant*", "D:/Clips/VALORANT/round.mp4"));
}

TEST(PatternMatching, NormalisesSeparators) {
	EXPECT_TRUE(u::matches_pattern("D:/clips/*", "D:\\clips\\round.mp4"));
	EXPECT_TRUE(u::matches_pattern("D:\\clips\\*", "D:/clips/round.mp4"));
}

TEST(PatternMatching, EmptyPatternNeverMatches) {
	EXPECT_FALSE(u::matches_pattern("", "D:/clips/round.mp4"));
	EXPECT_FALSE(u::matches_pattern("", ""));
}

TEST(PatternMatching, EdgeCases) {
	EXPECT_TRUE(u::matches_pattern("*", "D:/clips/round.mp4"));
	EXPECT_TRUE(u::matches_pattern("**", "D:/clips/round.mp4"));
	EXPECT_TRUE(u::matches_pattern("*", ""));
	EXPECT_TRUE(u::matches_pattern("*clips*round*", "D:/clips/valorant/round.mp4"));
	EXPECT_FALSE(u::matches_pattern("*round*clips*", "D:/clips/valorant/round.mp4"));
	EXPECT_TRUE(u::matches_pattern("d:/clips/round.mp4", "D:/clips/round.mp4"));
}

TEST(ValidateFilename, TrimsTheName) {
	EXPECT_EQ(u::validate_filename("  apex  "), "apex");
}

TEST(ValidateFilename, RejectsEmptyNames) {
	EXPECT_FALSE(u::validate_filename("").has_value());
	EXPECT_FALSE(u::validate_filename("   ").has_value());
}

TEST(ValidateFilename, RejectsWhatWindowsCantName) {
	for (const auto* name : { ".", "..", "apex.", "a/b", "a\b", "a:b", "a*b", "a?b", "a\"b", "a<b", "a|b", "a\tb" })
		EXPECT_FALSE(u::validate_filename(name).has_value()) << name;
}

TEST(ValidateFilename, AllowsOtherPunctuation) {
	EXPECT_EQ(u::validate_filename("apex (60fps) - v2.1"), "apex (60fps) - v2.1");
}

TEST(PatternMatching, HandlesNonAsciiPaths) {
	EXPECT_TRUE(u::matches_pattern("*clips*", "D:/vidéos/クリップ/clips/round.mp4"));
	EXPECT_TRUE(u::matches_pattern("*クリップ*", "D:/vidéos/クリップ/round.mp4"));
	EXPECT_FALSE(u::matches_pattern("*apex*", "D:/vidéos/クリップ/round.mp4"));
}

TEST(StringUtils, ToLowerLeavesNonAsciiAlone) {
	EXPECT_EQ(u::to_lower("Apex VIDÉO クリップ"), "apex vidÉo クリップ");
}

TEST(StringUtils, SplitKeepsEmptyParts) {
	using Parts = std::vector<std::string>;

	EXPECT_EQ(u::split_string("1,,2", ","), (Parts{ "1", "", "2" }));
	EXPECT_EQ(u::split_string("", ","), (Parts{ "" }));
	EXPECT_EQ(u::split_string("60000/1001", "/"), (Parts{ "60000", "1001" }));
}
