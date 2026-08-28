#pragma once

// Masks mark regions of the frame that should be protected from interpolation. The interpolator has no idea
// that a HUD isn't part of the scene, so it warps it along with the world behind it; a mask lets the renderer
// put the pre-interpolation pixels back over those regions afterwards.
//
// A mask is a png in <settings path>/masks - white where the frame should be interpolated as normal, black
// where it should be left alone. Settings store the bare filename, so a config stays portable between
// machines, and blur.py resolves it against the settings path it's already given.
namespace masks {
	inline const std::string FOLDER_NAME = "masks";

	// shown in the mask dropdowns, and what an empty setting means
	inline const std::string NONE_OPTION = "none";

	std::filesystem::path get_path();

	// filenames of every png in the masks folder, sorted. empty if the folder doesn't exist
	std::vector<std::string> list();

	// what a mask dropdown shows: "none", then every mask in the folder. `current` is kept in the list even if
	// it's been deleted since it was picked, so that's visible instead of the dropdown silently snapping to
	// something else
	std::vector<std::string> options(const std::string& current);
}
