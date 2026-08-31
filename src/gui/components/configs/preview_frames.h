#pragma once

#include "common/config_app.h"
#include "common/config_blur.h"
#include "../../render/render.h"

// the images behind the config preview screen.
//
// three of them: the sample video put through the blur pipeline (seconds per frame), the mask that render would
// apply, shown on its own when it's asked for, and - while the seek bar is being dragged - a plain frame straight
// from the source video (tens of milliseconds), which is quick enough to keep up with the drag. all the work happens
// on background threads, the ui just asks for whatever's ready.
//
// each of them keeps the last frame it rendered, so switching between the blurred preview and the mask shows what's
// already there straight away rather than rendering it again
namespace gui::components::configs::preview_frames {
	// built fresh every ui frame, so it just borrows what it's given
	struct Request {
		// the sample video, or empty when there isn't a usable one - the preview then tears itself down
		const std::filesystem::path& video_path;
		const BlurSettings& settings;
		const GlobalAppSettings& app_settings; // config_preview_seek is where the seek comes from
		bool seeking = false;                  // the seek bar is being dragged

		// show the mask the render would apply instead of the render itself
		bool show_mask = false;
	};

	struct Frame {
		std::shared_ptr<render::Texture> texture;
		std::string image_id;    // changes whenever the texture does, for ui::add_image
		bool up_to_date = false; // false while it's stale or still rendering - draw it faded
	};

	struct Result {
		std::optional<Frame> frame;  // the best frame there is to show right now, if there's one at all
		bool rendering = false;      // a frame of what was asked for is being rendered
		bool analysing_mask = false; // and it's stuck on working an automatic mask out, which is the slow part
		float video_duration = 0.f;  // 0 until the video's been read
	};

	// call once per ui frame, from the render thread. starts whatever the request needs rendering and hands back
	// whatever there is to show
	Result update(const Request& request);

	// A copy of the mask image currently shown by the preview, encoded as JPEG. Empty until a mask frame has
	// finished rendering. Callers should only offer it for saving when Result::frame is up to date.
	std::vector<uint8_t> current_mask_jpeg();

	void reset();
}
