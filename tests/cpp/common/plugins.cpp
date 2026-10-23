#include "helpers.h"

#include "common/plugins.h"

namespace {
	void make_install(const std::filesystem::path& library) {
		auto folder = library / "steamapps/common/Lossless Scaling";
		std::filesystem::create_directories(folder);
		std::ofstream(folder / "Lossless.dll") << "x";
	}

	// steam writes the paths with their backslashes escaped
	void write_library_folders(
		const std::filesystem::path& steam, const std::vector<std::filesystem::path>& libraries
	) {
		std::filesystem::create_directories(steam / "steamapps");
		std::ofstream vdf(steam / "steamapps/libraryfolders.vdf");
		vdf << "\"libraryfolders\"\n{\n";
		for (size_t i = 0; i < libraries.size(); i++) {
			std::string path = u::path_to_string(libraries[i].lexically_normal().make_preferred());
			std::string escaped;
			for (char c : path) {
				if (c == '\\')
					escaped += '\\';
				escaped += c;
			}
			vdf << "\t\"" << i << "\"\n\t{\n\t\t\"path\"\t\t\"" << escaped << "\"\n\t\t\"label\"\t\t\"\"\n\t}\n";
		}
		vdf << "}\n";
	}
}

TEST_F(TempDirTest, LosslessScalingIsFoundInAnotherSteamLibrary) {
	auto steam = m_test_dir / "steam";
	auto games = m_test_dir / "games library";
	make_install(games);
	write_library_folders(steam, { steam, games });

	auto found = plugins::lsfg::find_dll_in_steam(steam);

	ASSERT_TRUE(found);
	EXPECT_TRUE(std::filesystem::equivalent(*found, games / "steamapps/common/Lossless Scaling/Lossless.dll"));
}

TEST_F(TempDirTest, LosslessScalingIsFoundInSteamsOwnLibraryWithoutAList) {
	auto steam = m_test_dir / "steam";
	make_install(steam);

	auto found = plugins::lsfg::find_dll_in_steam(steam);

	ASSERT_TRUE(found);
	EXPECT_TRUE(std::filesystem::equivalent(*found, steam / "steamapps/common/Lossless Scaling/Lossless.dll"));
}

TEST_F(TempDirTest, LosslessScalingIsntFoundInLibrariesWithoutIt) {
	auto steam = m_test_dir / "steam";
	write_library_folders(steam, { steam, m_test_dir / "games library" });

	EXPECT_FALSE(plugins::lsfg::find_dll_in_steam(steam));
}

TEST_F(TempDirTest, LosslessScalingFolderFindsItsDll) {
	auto folder = m_test_dir / "Lossless Scaling";
	std::filesystem::create_directories(folder);
	std::ofstream(folder / "Lossless.dll") << "x";

	EXPECT_EQ(plugins::lsfg::find_dll(u::path_to_string(folder)), folder / "Lossless.dll");
	EXPECT_EQ(plugins::lsfg::find_dll(u::path_to_string(folder / "Lossless.dll")), folder / "Lossless.dll");
}

TEST_F(TempDirTest, LosslessScalingPathWithoutTheDllIsntFound) {
	EXPECT_FALSE(plugins::lsfg::find_dll(u::path_to_string(m_test_dir)));
	EXPECT_FALSE(plugins::lsfg::find_dll(u::path_to_string(m_test_dir / "missing" / "Lossless.dll")));
}
