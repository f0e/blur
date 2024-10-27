#pragma once

#include "rendering.h"

class c_blur {
public:
	const std::string BLUR_VERSION = "1.94";

	bool verbose = true;
	bool using_preview = false;

	std::filesystem::path path;
	bool used_installer = false;

	std::unordered_set<std::filesystem::path> created_temp_paths; // atexit cant take params -_-

public:
	bool initialise(bool _verbose, bool _using_preview);
	void cleanup();
};

inline c_blur blur;