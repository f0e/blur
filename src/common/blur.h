#pragma once

const std::string APPLICATION_NAME = "blur";
const std::string BLUR_VERSION = "1.94";

class Blur { // todo: switch all the classes which could be namespaces into namespaces
public:
	bool verbose = true;
	bool using_preview = false;

	std::filesystem::path temp_path;

	std::filesystem::path path;
	bool used_installer = false;

	std::filesystem::path ffmpeg_path;
	std::filesystem::path vspipe_path;

	bool initialise(bool _verbose, bool _using_preview);
	void cleanup() const;

	void initialise_base_temp_path();

	std::optional<std::filesystem::path> create_temp_path(const std::string& folder_name) const;
	static bool remove_temp_path(const std::filesystem::path& temp_path);
};

inline Blur blur;
