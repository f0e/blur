#pragma once

#include "render_types.h"
#include "render_errors.h"
#include "render_state.h"

// runs a vspipe | ffmpeg pipeline, handling progress, preview frames, errors and pause/stop
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
