#pragma once

namespace tasks {
	inline int finished_renders = 0;

	void run(const std::vector<std::string>& arguments);

	void add_files_for_render(const std::vector<std::filesystem::path>& path_strs);
	void add_sample_video(const std::filesystem::path& path_str);
	void process_pending_files();
}
