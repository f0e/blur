#pragma once

#include "rendering.h"

class c_blur {
public:
	const std::string BLUR_VERSION = "1.93";

	bool verbose = true;
	bool using_preview = false;

	std::filesystem::path path;
	bool used_installer = false;

	std::filesystem::path temp_path;

public:
	bool initialise(bool _verbose, bool _using_preview);

	bool create_temp_path();
	bool remove_temp_path();
};

inline c_blur blur;