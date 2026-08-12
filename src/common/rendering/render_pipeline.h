#pragma once

#include "render_types.h"
#include "render_state.h"

// Runs a built vspipe | ffmpeg pipeline: spawns the two processes, streams
// their stderr/stdout (progress, preview frames, errors) and handles
// pause/stop until they finish.
namespace rendering::detail {
	struct PipelineResult {
		bool stopped;
	};

	tl::expected<PipelineResult, RenderError> execute_pipeline(
		const RenderCommands& commands,
		const std::shared_ptr<RenderState>& state,
		bool debug,
		bool audio,
		const std::function<void()>& progress_callback
	);
}
