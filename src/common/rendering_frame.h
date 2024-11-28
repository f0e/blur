#pragma once

#include "config.h"
#include "rendering.h"

class FrameRender {
	boost::process::child vspipe_process;
	boost::process::child ffmpeg_process;
	bool running = false;

public:
	bool do_render(RenderCommands render_commands, const BlurSettings& settings);

	struct RenderResponse {
		bool success;
		std::filesystem::path output_path;
	};

	RenderResponse render(const std::filesystem::path& input_path, const BlurSettings& settings);

	RenderCommands build_render_commands(
		const std::filesystem::path& input_path, const std::filesystem::path& output_path, const BlurSettings& settings
	);

	void kill();
};
