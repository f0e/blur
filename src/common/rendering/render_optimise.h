#pragma once

#include "render_types.h"
#include "render_state.h"
#include "common/config_app.h"
#include "common/config_blur.h"
#include "common/media.h"

// picks svp's gpu settings by timing each combination on the video being rendered. whether the gpu or the cpu is the
// bottleneck depends on the hardware, the settings and the video, so the pick is saved per combination of those
namespace rendering::optimise {
	struct GpuSettings {
		bool svp_gpu_search = false;
		bool svp_gpu_rendering = true;
		bool gpu_decoding = false;

		bool operator==(const GpuSettings& other) const = default;
	};

	struct SavedPick {
		std::string video;
		GpuSettings settings;
	};

	// whether there's anything to time with these settings, or it always uses the default
	bool benchmarks(const BlurSettings& settings);

	using BuildCommands = std::function<tl::expected<RenderCommands, std::string>(
		const BlurSettings& settings, const std::filesystem::path& output_path
	)>;

	// benchmarks if nothing's saved for this video yet
	BlurSettings for_render(
		const BlurSettings& settings,
		const GlobalAppSettings& app_settings,
		const media::VideoInfo& video_info,
		const std::shared_ptr<RenderState>& state,
		const std::string& output_extension,
		const BuildCommands& build_commands
	);

	// the saved pick or the default, never benchmarks
	BlurSettings for_preview(
		const BlurSettings& settings, const GlobalAppSettings& app_settings, const media::VideoInfo& video_info
	);

	// what's been picked for videos rendered with these settings
	std::vector<SavedPick> list_saved(const BlurSettings& settings, const GlobalAppSettings& app_settings);

	void clear_saved(const BlurSettings& settings, const GlobalAppSettings& app_settings);
}
