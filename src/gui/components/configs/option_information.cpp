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
				"Also covers deduplication, which fills dropped frames by interpolating them",
				"Masks are image files in the masks folder of your config folder",
				"White where the video should be interpolated as normal, black where it should be left alone",
				"Masked areas still get motion blur - only interpolation skips them",
			},
		},
		{
			"default auto mask checkbox",
			{
				"Builds an extra mask per video by finding the parts of its frame that never move",
				"Applied on top of the mask above, so a HUD you've already masked stays masked either way",
				"Use the mask above for what's the same in every video, and this for whatever each one adds",
				"Analysing a video takes a moment, but the result is cached and reused",
			},
		},

		// Automatic mask tuning
		{
			"auto mask stillness slider",
			{
				"How much of the time a pixel has to stay static",
			},
		},
		{
			"auto mask fill slider",
			{
				"How far the mask fills into a flat area from the detail around it",
			},
		},
		{
			"auto mask padding slider",
			{
				"How far the mask spreads past what was found",
			},
		},
		{
			"auto mask feather slider",
			{
				"How far the edge of the mask fades out",
			},
		},
		{
			"auto mask samples slider",
			{
				"How many frames from across the video get compared to find the parts that never move",
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
				"Ignores duplicate frames and generates what should have been there instead, from the nearest frames "
				"that aren't repeats",
				"(fixes 'unsmooth' looking output caused by stuttering in recordings)",
				"With interpolation on this happens in the same pass, so every generated frame comes from frames "
				"that were really captured",
			},
		},
		{
			"deduplicate range",
			{
				"How far apart two frames can be and still have frames generated between them",
				"Make it higher if your footage is at a lower FPS than it should be, e.g. choppy 120fps gameplay "
				"recorded at 240fps",
				"Lower it if your blurred footage starts blurring static elements such as menu screens",
			},
		},
		{
			"deduplicate threshold input",
			{
				"Threshold of movement that triggers deduplication",
				"Turn on debug in advanced and render a video to label every frame deduplication had a hand in with "
				"the movement it measured and the frames it worked from",
				"Turn blur off to read it - blending averages the text away along with everything else",
			},
		},
		{
			"deduplicate real frame dropdown",
			{
				"When frames are dropped the recording repeats one to fill the slots, and nothing in the file says "
				"which frame of the run is the picture that was really drawn. This is that answer.",

				"'first' is what a live recording does - the picture is drawn, then held until the next one is "
				"ready. Leave it here unless you have a reason not to.",

				"'last' suits footage where the run ends on the real frame instead, which is what a variable "
				"framerate recording resampled to a fixed one can look like.",

				"'center' splits the difference and puts the picture in the middle of its run. It can't be more "
				"than half a run out whichever way the footage leans, where picking the wrong end can be a whole "
				"run out.",

				"'surrounding' doesn't believe the run at all and works from the frames either side of it, which "
				"comes out right whichever way the footage leans. It needs runs of one frame to work from, so it "
				"suits stuttery footage rather than a game running at a clean half of the recording framerate, and "
				"it generates across a longer gap - more for the interpolator to get wrong. Raise 'deduplicate "
				"range' to give it room.",

				"This only makes a difference where runs of duplicates vary in length. Getting it wrong there shows "
				"up as motion that speeds up and slows down rather than running at a steady rate.",
			},
		},
		{
			"max future checks slider",
			{
				"How many times 'surrounding' may step over a run of duplicates that is itself in question, "
				"looking for a frame whose timing isn't.",
				"Each step widens the gap it generates across, and the search still stops at 'deduplicate range'.",
			},
		},
		{
			"deduplicate method dropdown",
			{
				"What generates the frames that go in place of duplicates. Only needed with interpolation off - "
				"with it on, the interpolation method generates them as part of its own pass.",
				// todo: update with mvtools
				"Quality: rife = rife (tensorrt) > svp",
				"Speed: old > svp >>> rife",
				"rife (tensorrt) is probably slower than rife here, but it'll depend on your gpu.",
			},
		},
		{
			"deduplicate method interpolation note",
			{
				"Duplicates are filled by the interpolation pass, from the same model, in one go - so there's no "
				"separate method to choose while interpolation is on.",
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
			"preview hardware decoding checkbox",
			{
				"Decodes previewed videos (in the queue) on the gpu. Will usually be faster, but not always.",
				"Try toggling it if the preview is choppy",
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
			"taskbar progress checkbox",
			{
				"Shows how far along the current render is on the app\x27s taskbar icon",
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
