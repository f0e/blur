#pragma once

#include "ui/ui.h"

// saving the frame a preview's showing
namespace frame_export {
	// the save dialog starts next to the video, named after it
	std::vector<ui::ContextMenuItem> menu_items(
		const ui::Frame& frame, const std::filesystem::path& video_path, bool mask = false
	);
}
