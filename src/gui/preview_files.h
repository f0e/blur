#pragma once

// files the previews write, like scripts and samples, which go in a temp folder for this run
namespace preview_files {
	std::filesystem::path new_path(const std::string& extension);

	// for shutdown, once nothing has the files open
	void remove_all();

	// ones left by runs that didn't get to shut down
	void remove_stale();
}
