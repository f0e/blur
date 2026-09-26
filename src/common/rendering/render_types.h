#pragma once

struct RenderCommands {
	std::vector<std::string> vspipe_video;
	std::vector<std::string> ffmpeg;

	// ffmpeg exits before vspipe is done, so vspipe's exit code can be ignored
	bool ffmpeg_stops_early = false;
};

namespace rendering {
	struct RenderResult {
		std::filesystem::path output_path;
		bool stopped = false;
	};
}
