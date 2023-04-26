#pragma once

#include <string>
#include <cxxopts/cxxopts.hpp>

#include "rendering.h"

class c_blur {
public:
	const std::string BLUR_VERSION = "1.9";

	bool using_ui = true;
	bool verbose = true;
	bool no_preview = false;

	std::filesystem::path path;
	bool used_installer = false;

private:
	std::vector<std::string> get_files(int& argc, char* argv[]);

public:
	std::filesystem::path create_temp_path(const std::filesystem::path& video_path);
	void remove_temp_path(const std::filesystem::path& path);

public:
	void run(int argc, char* argv[], const cxxopts::ParseResult& cmd);
	void run_gui();
};

inline c_blur blur;