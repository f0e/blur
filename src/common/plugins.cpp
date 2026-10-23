#include "plugins.h"
#include "config_base.h"
#include "paths.h"

namespace {
	// the builds with these only exist for windows
	bool plugin_installed(const std::string& name) {
#ifdef _WIN32
		std::error_code ec;
		return std::filesystem::exists(
			paths::get_resources_path() / "lib/vapoursynth/vs-plugins" / (name + ".dll"), ec
		);
#else
		return false;
#endif
	}

	const std::filesystem::path LOSSLESS_SCALING_FOLDER = "steamapps/common/Lossless Scaling";
	constexpr std::string_view LOSSLESS_SCALING_DLL = "Lossless.dll";

#ifdef _WIN32
	std::optional<std::filesystem::path> steam_path() {
		std::array<wchar_t, MAX_PATH> buffer{};
		DWORD size = sizeof(buffer);
		if (RegGetValueW(
				HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buffer.data(), &size
			) != ERROR_SUCCESS)
			return {};

		return std::filesystem::path(buffer.data());
	}
#endif
}

bool plugins::lsfg::installed() {
	static const bool result = plugin_installed("lsfg");
	return result;
}

std::optional<std::filesystem::path> plugins::lsfg::find_dll_in_steam(const std::filesystem::path& steam_folder) {
	static const std::regex path_line(R"re("path"\s+"((?:[^"\\]|\\.)*)")re");

	std::vector<std::filesystem::path> libraries = { steam_folder };

	std::string vdf = config_base::read_config_file(steam_folder / "steamapps/libraryfolders.vdf").value_or("");
	for (auto it = std::sregex_iterator(vdf.begin(), vdf.end(), path_line); it != std::sregex_iterator(); ++it) {
		std::string escaped = (*it)[1].str();

		std::string path;
		for (size_t i = 0; i < escaped.size(); i++) {
			if (escaped[i] == '\\' && i + 1 < escaped.size())
				i++;
			path += escaped[i];
		}

		libraries.emplace_back(u::string_to_path(path));
	}

	std::error_code ec;
	for (const auto& library : libraries) {
		auto path = library / LOSSLESS_SCALING_FOLDER / LOSSLESS_SCALING_DLL;
		if (std::filesystem::is_regular_file(path, ec))
			return path;
	}

	return {};
}

std::optional<std::filesystem::path> plugins::lsfg::find_dll(const std::string& folder_or_dll) {
	if (!folder_or_dll.empty()) {
		std::error_code ec;
		std::filesystem::path path = u::string_to_path(folder_or_dll);
		if (std::filesystem::is_directory(path, ec))
			path /= LOSSLESS_SCALING_DLL;

		if (std::filesystem::is_regular_file(path, ec))
			return path;
		return {};
	}

#ifdef _WIN32
	if (auto steam = steam_path())
		return find_dll_in_steam(*steam);
#endif

	return {};
}
