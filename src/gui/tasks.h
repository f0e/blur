#pragma once

namespace tasks {
	void run(const std::vector<std::string>& arguments);

	void add_files(const std::vector<std::string>& path_strs);
	void add_sample_video(const std::string& path_str);
}
