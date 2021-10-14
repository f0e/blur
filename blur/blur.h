#pragma once

#include <string>
#include <cxxopts/cxxopts.hpp>

#include "rendering.h"

class c_blur {
public:
	bool using_ui = true;
	bool verbose = true;
	bool no_preview = false;

	std::string path;
	bool used_installer = false;

private:
	std::vector<std::string> get_files(int& argc, char* argv[]);

public:
	std::string create_temp_path(const std::string& video_path);
	void remove_temp_path(const std::string& path);

public:
	void run(int argc, char* argv[], const cxxopts::ParseResult& cmd);
};

inline c_blur blur;