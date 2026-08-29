#pragma once

#include "common/config_app.h"

// the two subprocess argument vectors a render is made of
struct RenderCommands {
	std::vector<std::string> vspipe_video;
	std::vector<std::string> ffmpeg;

	// set when ffmpeg is asked for a fixed number of frames rather than the whole clip, so it exits while
	// vspipe is still feeding it and vspipe gets terminated mid-stream. that makes vspipe's exit code
	// meaningless, so execute_pipeline stops holding it against the render
	bool ffmpeg_stops_early = false;
};

namespace rendering {
	struct RenderResult {
		std::filesystem::path output_path;
		bool stopped = false;
	};

	using RenderError = u::ParsedError;
}
