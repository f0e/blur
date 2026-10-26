#pragma once

namespace plugins::lsfg {
	bool installed();
	std::optional<std::filesystem::path> find_dll(const std::string& folder_or_dll);
	std::optional<std::filesystem::path> find_dll_in_steam(const std::filesystem::path& steam_folder);
}
