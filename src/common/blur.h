#pragma once

#include "rendering.h"

class c_blur {
public:
	const std::string BLUR_VERSION = "1.9";

	bool verbose = true;
	bool using_preview = false;

	std::filesystem::path path;
	bool used_installer = false;

private:
	std::vector<std::string> get_files(int& argc, char* argv[]);

public:
	std::filesystem::path create_temp_path(const std::filesystem::path& video_path);
	void remove_temp_path(const std::filesystem::path& path);

public:
	bool initialise(bool _verbose, bool _using_preview);
};

inline c_blur blur;