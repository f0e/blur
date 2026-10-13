#pragma once

#include "ui/ui.h"

// the rows shown by a video's info icon. include_audio is for previews that play it, the config preview is muted
std::vector<ui::InfoRow> get_video_info_rows(const media::VideoInfo& info, bool include_audio);
