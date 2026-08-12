#pragma once

#include "common/config_app.h"

// the two subprocess argument vectors a render is made of
struct RenderCommands {
	std::vector<std::string> vspipe_video;
	std::vector<std::string> ffmpeg;
};

namespace rendering {
	struct RenderResult {
		std::filesystem::path output_path;
		bool stopped = false;
	};

	using RenderError = u::ParsedError;
}
