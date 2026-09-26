#pragma once

// masks protect regions of the frame (like a hud) from interpolation and deduplication. black is protected, white is
// interpolated as normal
//
// the base mask is picked in the config and the auto mask is generated per video from the parts that never move. they
// stack, so a pixel is protected if either one protects it. auto masks are saved as pngs in auto-masks so they can be
// copied into the masks folder and touched up
namespace masks {
	inline const std::string FOLDER_NAME = "masks";

	// shown in the mask dropdowns, and what an empty setting means
	inline const std::string NONE_OPTION = "none";

	std::filesystem::path get_path();

	std::vector<std::string> list();

	// includes `current` even if it's since been deleted
	std::vector<std::string> options(const std::string& current);
}
