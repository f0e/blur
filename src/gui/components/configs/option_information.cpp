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
			"preserve brightness checkbox",
			{
				"Blends frames by how much light they hold, rather than averaging the stored values",
				"Produces a more natural-looking blur",
			},
		},
		{
			"bloom checkbox",
			{
				"Makes bright areas glow, brightening pixels around them",
			},
		},
		{
			"bloom threshold",
			{
				"How bright something has to be before it glows",
				"(0 = everything glows, 1 = only the very brightest)",
			},
		},
		{
			"bloom strength",
			{
				"How bright the glow is",
			},
		},
		{
			"blur weighting gaussian std dev slider",
			{
				"Standard deviation for Gaussian blur weighting",
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
			"default auto mask checkbox",
			{
				"Uses an extra automatic mask",
				"Configured in the advanced section",
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
				"How many frames are sampled to find the parts that never move",
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
#ifdef TENSORRT
		{
			"pre-interpolation method dropdown",
			{
				"Quality: rife = rife (tensorrt)",
				"Speed: rife (tensorrt) > rife",
			},
		},
#endif
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
				"(Higher = less accurate, faster; lower = more accurate, slower)",
			},
		},
		{
			"interpolation mask area slider",
			{
				"Mask amount for interpolation",
				"(Higher reduces blur on static objects but can affect smoothness)",
			},
		},

		// Rendering settings
		{
			"deduplicate checkbox",
			{
				"Removes duplicate frames and replaces them with interpolated frames",
				"(Fixes 'unsmooth' looking output caused by stuttering in recordings)",
			},
		},
		{
			"deduplicate range",
			{
				"How far apart two frames can be and still have frames generated between them",
				"Make it higher if your footage is at a lower FPS than it should be, e.g. choppy 120fps gameplay recorded at 240fps",
				"Lower it if your blurred footage starts blurring static elements such as menu screens",
			},
		},
		{
			"deduplicate threshold input",
			{
				"Threshold of movement that triggers deduplication",
			},
		},
		{
			"deduplicate real frame dropdown",
			{
				"Which frame in a run of duplicates is the real one",
				"first: The first frame (default)",
				"last: The last frame",
				"center: The middle frame",
				"surrounding: Ignores all the frames and uses the frames either side of the duplicates. Will give the smoothest result but can lead to a lot of artifacting",
			},
		},
		{
			"max future checks slider",
			{
				"How many following runs of duplicates 'surrounding' can skip over",
				"(Limited by deduplicate range)",
			},
		},
		{
			"frame timing logs checkbox",
			{
				"For use with the obs-frame-timing-recorder OBS plugin",
				"Uses the .frametiming files it generates to work out when each frame was really drawn, resulting in much smoother blur",
				"This also replaces deduplication, as it's more accurate",
			},
		},
		{
			"deduplicate method dropdown",
			{
				"Method used to replace duplicate frames (only used when interpolation is off)",
				// todo: update with mvtools
				"Quality: rife = rife (tensorrt) > svp",
				"Speed: old > svp >>> rife",
				"(rife (tensorrt) is probably slower than rife here, depends on your GPU)",
			},
		},
		{
			"deduplicate method interpolation note",
			{
				"Duplicates are replaced by the interpolation method while interpolation is on",
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
				"Note: GPU decoding can cause issues",
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
				"(Overrides GPU & quality options)",
			},
		},
		{
			"debug checkbox",
			{
				"Logs FFmpeg & vspipe commands, and adds a text overlay displaying frame similarity onto duplicate frames",
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
				"Decodes previewed videos (in the queue and config preview) on the GPU. Will usually be faster, but not always",
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
				"Shows how far along the current render is on the app's taskbar icon",
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
