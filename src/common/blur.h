#pragma once

const std::string BLUR_VERSION = "1.94";

class Blur { // todo: switch all the classes which could be namespaces into namespaces
public:
	bool verbose = true;
	bool using_preview = false;

	std::filesystem::path path;
	bool used_installer = false;

	std::filesystem::path ffmpeg_path;
	std::filesystem::path vspipe_path;

	std::unordered_set<std::filesystem::path> created_temp_paths; // atexit cant take params -_-

	bool initialise(bool _verbose, bool _using_preview);
	void cleanup();
};

inline Blur blur;
