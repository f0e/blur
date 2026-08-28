#include "masks.h"

std::filesystem::path masks::get_path() {
	return blur.settings_path / FOLDER_NAME;
}

std::vector<std::string> masks::list() {
	std::vector<std::string> masks;

	auto path = get_path();

	std::error_code ec; // don't throw if the folder's missing or unreadable, just show nothing
	for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
		if (!entry.is_regular_file(ec))
			continue;

		if (u::to_lower(u::path_to_string(entry.path().extension())) != ".png")
			continue;

		masks.push_back(u::path_to_string(entry.path().filename()));
	}

	std::ranges::sort(masks);

	return masks;
}

std::vector<std::string> masks::options(const std::string& current) {
	auto mask_files = list();

	std::vector<std::string> options = { NONE_OPTION };
	options.insert(options.end(), mask_files.begin(), mask_files.end());

	if (!current.empty() && !u::contains(options, current))
		options.push_back(current);

	return options;
}
