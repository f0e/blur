#pragma once

// Masks mark regions of the frame that should be protected from interpolation. The interpolator has no idea
// that a HUD isn't part of the scene, so it warps it along with the world behind it; a mask lets the renderer
// put the original pixels back over those regions afterwards. Deduplication counts as interpolation here - it
// fills a dropped frame by interpolating one - so a mask covers that too.
//
// A mask is an image in <settings path>/masks - white where the frame should be interpolated as normal, black
// where it should be left alone. Settings store the bare filename, so a config stays portable between
// machines, and blur.py resolves it against the settings path it's already given.
//
// There are two of them, and they stack. The base mask is the file picked in the settings - a HUD that sits in
// the same place in every video of a game gets drawn once and reused forever. The automatic mask is worked out
// from each video on its own, by finding the parts of the frame that never move, and catches whatever that
// particular video has on top of the base. Either can be used without the other; with both, a pixel is
// protected if either of them protects it.
//
// Automatic masks are written as pngs into <settings path>/auto-masks, so one can be copied over into the
// masks folder, touched up and kept like any other - which is a reasonable way to author a base mask. That
// folder holds nothing else; the measurements they're worked out from are an intermediate, and live under
// <settings path>/auto-mask-cache instead.
namespace masks {
	inline const std::string FOLDER_NAME = "masks";

	// shown in the mask dropdowns, and what an empty setting means
	inline const std::string NONE_OPTION = "none";

	std::filesystem::path get_path();

	// filenames of every image in the masks folder, sorted. empty if the folder doesn't exist
	std::vector<std::string> list();

	// what a mask dropdown shows: "none", then every mask in the folder. `current` is kept in the list even if
	// it's been deleted since it was picked, so that's visible instead of the dropdown silently snapping to
	// something else
	std::vector<std::string> options(const std::string& current);
}
