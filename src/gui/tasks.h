#pragma once

namespace tasks {
	inline int finished_renders = 0;
	inline std::filesystem::path video_player_path = "";

	void run(const std::vector<std::string>& arguments);

	void add_files_for_render(const std::vector<std::filesystem::path>& path_strs);
	void add_sample_video(const std::filesystem::path& path_str);
	void process_pending_files();
	void set_video_player_path(const std::filesystem::path& path_str);
}
