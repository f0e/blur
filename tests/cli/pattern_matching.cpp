#include "tests.h"

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

// a pattern with a wildcard has to match the whole path, so anything anchored mid-path needs a
// leading '*'. a pattern without one is a substring, which is what most rules end up being
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
