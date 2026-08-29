#include "configs.h"

#include "../../ui/ui.h"
#include "../../render/render.h"

namespace configs = gui::components::configs;

void configs::option_information(ui::Container& container) {
	const static std::unordered_map<std::string, std::vector<std::string>> option_explanations = {
		// Blur settings
		// { "section blur checkbox",
		//   {
		// 	  "Enable motion blur",
		//   }, },
		{
			"blur amount",
			{
				"Amount of motion blur",
				"(0 = no blur, 1 = fully blend all frames, >1 = blend extra frames (ghosting))",
			},
		},
		// { "output fps",
		//   {
		// 	  "FPS of the output video",
		//   }, },
		{
			"blur gamma",
			{
				"Amount that the video is darkened before blurring. Makes highlights stand out",
			},
		},
		{
			"blur weighting gaussian std dev slider",
			{
				"Standard deviation for Gaussian blur weighting",
			},
		},
		{
			"blur weighting triangle reverse checkbox",
			{
				"Reverses the direction of triangle weighting",
			},
		},
		{
			"blur weighting bound input",
			{
				"Weighting bounds to spread weights more",
			},
		},

		// Interpolation settings
		// { "section interpolation checkbox",
		//   {
		// 	  "Enable interpolation to a higher FPS before blurring",
		//   }, },
		{
			"interpolate scale checkbox",
			{
				"Use a multiplier for FPS interpolation rather than a set FPS",
			},
		},
		{
			"interpolated fps mult",
			{
				"Multiplier for FPS interpolation",
				"The input video will be interpolated to this FPS (before blurring)",
			},
		},
		{
			"interpolated fps",
			{
				"FPS to interpolate input video to (before blurring)",
			},
		},
		{
			"default mask dropdown",
			{
				"Mask applied to videos you add, protecting parts of the frame from interpolation",
				"Useful for static overlays like a HUD, which interpolation tends to warp",
				"'auto' builds a mask per video by finding the parts of the frame that never move",
				"Otherwise, masks are png files in the masks folder of your config folder",
				"White where the video should be interpolated as normal, black where it should be left alone",
				"Masked areas still get motion blur - only interpolation skips them",
			},
		},
		{
			"interpolation method dropdown",
			{
#ifdef TENSORRT
				// todo: update with mvtools
				"Quality: rife = rife (tensorrt) > svp",
				"Speed: svp >> rife (tensorrt) > rife",
#else
				// todo: update with mvtools
				"Quality: rife > svp",
				"Speed: svp >>> rife",
#endif
			},
		},
		// pre-interp settings
		{
			"section pre-interpolation checkbox",
			{
				"Enable pre-interpolation using a more accurate but slower AI model before main interpolation",
			},
		},
		{
			"pre-interpolated fps mult",
			{
				"Multiplier for FPS pre-interpolation",
				"The input video will be interpolated to this FPS (before main interpolation and blurring)",
			},
		},
		{
			"pre-interpolated fps",
			{
				"FPS to pre-interpolate input video to (before blurring)",
			},
		},
		{
			"SVP interpolation preset dropdown",
			{
				"Check the blur GitHub for more information",
			},
		},
		{
			"SVP interpolation algorithm dropdown",
			{
				"Check the blur GitHub for more information",
			},
		},
		{
			"interpolation block size dropdown",
			{
				"Block size for interpolation",
				"(higher = less accurate, faster; lower = more accurate, slower)",
			},
		},
		{
			"interpolation mask area slider",
			{
				"Mask amount for interpolation",
				"(higher reduces blur on static objects but can affect smoothness)",
			},
		},

		// Rendering settings
		{
			"deduplicate checkbox",
			{
				"Removes duplicate frames and replaces them with interpolated frames",
				"(fixes 'unsmooth' looking output caused by stuttering in recordings)",
			},
		},
		{
			"deduplicate range",
			{
				"Amount of frames beyond the current frame to look for unique frames when deduplicating",
				"Make it higher if your footage is at a lower FPS than it should be, e.g. choppy 120fps gameplay "
				"recorded at 240fps",
				"Lower it if your blurred footage starts blurring static elements such as menu screens",
			},
		},
		{
			"deduplicate threshold input",
			{
				"Threshold of movement that triggers deduplication",
				"Turn on debug in advanced and render a video to embed text showing the movement in each frame",
			},
		},
		{
			"deduplicate frames to interpolate input",
			{
				"If 'previous to duplicate', duplicate frames after the first will be interpolated to the next unique "
				"frame. Since we don't know if duplicate frames are early or late, this may result in output not being "
				"as smooth as it can be. This is the default behaviour.",

				"If 'surrounding frames', the duplicate frame will be ignored, and the frames surrounding it will "
				"be interpolated. This will result in more interpolation artifacts, but smoother output.",

				"'surrounding frames + future check' is the same as above, but if the next frame (not of this "
				"duplicate set) is also a duplicate, it'll continue searching until it finds a truly non-duplicate "
				"frame. This will provide the smoothest output, but will produce a lot of artifacts if footage has a "
				"lot of duplicate frames.",

				"If 'duplicate to next', duplicate frames up to the last will be interpolated with the previous unique "
				"frame. This is similar to 'previous to duplicate', and the same drawbacks apply.",
			},
		},
		{
			"max future checks slider",
			{
				"Maximum amount of times future duplicate frames can be skipped when using 'surrounding frames + "
				"future check' for 'deduplicate frames to interpolate'. If this limit is passed, the first future "
				"duplicate is used for interpolation.",
			},
		},
		{
			"deduplicate method dropdown",
			{
				// todo: update with mvtools
				"Quality: rife = rife (tensorrt) > svp",
				"Speed: old > svp >>> rife",
				"rife (tensorrt) is probably slower than rife here, but it'll depend on your gpu.",
			},
		},
		{
			"upscale checkbox",
			{
				"Upscales to 4K using nearest-neighbour interpolation",
			},
		},
		{
			"preview checkbox",
			{
				"Shows preview while rendering",
			},
		},
		{
			"detailed filenames checkbox",
			{
				"Adds blur settings to generated filenames",
			},
		},

		// gpu acceleration
		{
			"gpu decoding",
			{
				"Note: GPU decoding can cause issues with colour handling",
			},
		},

		// Timescale settings
		// {
		// 	"section timescale checkbox",
		// 	{
		// 		"Enable video timescale manipulation",
		// 	},
		// },
		// {
		// 	"input timescale",
		// 	{
		// 		"Timescale of the input video file",
		// 	},
		// },
		// {
		// 	"output timescale",
		// 	{
		// 		"Timescale of the output video file",
		// 	},
		// },
		{
			"adjust timescaled audio pitch checkbox",
			{
				"Pitch shift audio when speeding up or slowing down video",
			},
		},

		// Filters
		// { "section filters checkbox", { "Enable video filters", }, },
		// { "brightness", { "Adjusts brightness of the output video", }, },
		// { "saturation", { "Adjusts saturation of the output video", }, },
		// { "contrast", { "Adjusts contrast of the output video", }, },

		// Advanced rendering
		// { "gpu interpolation checkbox", { "Uses GPU for interpolation", }, },
		// { "gpu encoding checkbox", { "Uses GPU for rendering", }, },
		// { "gpu encoding type dropdown", { "Select GPU type", }, },
		{
			"video container text input",
			{
				"Output video container format",
			},
		},
		{
			"custom ffmpeg filters text input",
			{
				"Custom FFmpeg filters for rendering",
				"(overrides GPU & quality options)",
			},
		},
		{
			"debug checkbox",
			{
				"Logs ffmpeg & vspipe commands, and adds a text overlay displaying frame similarity onto duplicate "
				"frames",
			},
		},
		{
			"resize chroma location dropdown",
			{
				"Sets the chroma location used when resizing. Can fix colours being moved slightly off from where "
				"they should be",
			},
		},
		{
			"copy dates checkbox",
			{
				"Copies over the modified date from the input",
			},
		},

		// App settings
		{
			"queue preview volume slider",
			{
				"Volume of videos previewed in the queue",
			},
		},
		{
			"skip queue checkbox",
			{
				"Starts rendering videos as soon as they're added instead of queueing them up",
			},
		},
		{
			"render success notifications checkbox",
			{
				"Sends a desktop notification when a render finishes",
			},
		},
		{
			"render failure notifications checkbox",
			{
				"Sends a desktop notification when a render fails",
			},
		},
		{
			"config override notification checkbox",
			{
				"Notifies you when a video is rendered using a config file next to it rather than the global config",
			},
		},
		{
			"clear dismissed update button",
			{
				"You dismissed this update, so you won't be notified about it again. Press this to be notified again",
			},
		},
#ifdef __linux__
		{
			"vapoursynth lib path input",
			{
				"Path to your VapourSynth libraries, used if they aren't in the default location",
			},
		},
#endif
	};

	std::string hovered = ui::get_hovered_id();

	if (hovered.empty())
		return;

	if (!option_explanations.contains(hovered))
		return;

	ui::add_hint("hovered option info", container, option_explanations.at(hovered), gfx::Color::white(), fonts::dejavu);
}
