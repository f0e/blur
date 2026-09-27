#pragma once

struct RenderCommands {
	std::vector<std::string> vspipe_video;
	std::vector<std::string> ffmpeg;
};

namespace rendering {
	struct RenderResult {
		std::filesystem::path output_path;
		bool stopped = false;
	};
}
