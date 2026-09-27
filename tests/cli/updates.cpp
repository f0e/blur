#include "tests.h"

#include "common/updates.h"

TEST(VersionComparison, ComparesMajor) {
	EXPECT_TRUE(updates::is_version_newer("3.9.9", "4.0.0"));
	EXPECT_FALSE(updates::is_version_newer("4.0.0", "3.9.9"));
}

TEST(VersionComparison, ComparesPartsAsNumbers) {
	EXPECT_TRUE(updates::is_version_newer("3.0.9", "3.0.10"));
	EXPECT_TRUE(updates::is_version_newer("3.9.1", "3.10.0"));
	EXPECT_FALSE(updates::is_version_newer("3.1.2", "3.1.1"));
}

TEST(VersionComparison, OldVersionsAreOlderThan3) {
	EXPECT_TRUE(updates::is_version_newer("2.45", "3.0.0"));
	EXPECT_TRUE(updates::is_version_newer("2.151", "3.0.0"));
	EXPECT_FALSE(updates::is_version_newer("3.0.0", "2.45"));
}

TEST(VersionComparison, IgnoresPrefix) {
	EXPECT_FALSE(updates::is_version_newer("v3.0.0", "3.0.0"));
}

TEST(VersionComparison, RejectsGarbage) {
	EXPECT_FALSE(updates::is_version_newer("3.0.0", "latest"));
	EXPECT_FALSE(updates::is_version_newer("", "3.0.0"));
}
