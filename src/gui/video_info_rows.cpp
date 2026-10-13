#include "video_info_rows.h"

std::vector<ui::InfoRow> get_video_info_rows(const media::VideoInfo& info, bool include_audio) {
	std::vector<ui::InfoRow> rows;

	rows.push_back({ .label = "resolution", .value = std::format("{}x{}", info.width, info.height) });

	if (info.fps_num > 0 && info.fps_den > 0)
		rows.push_back(
			{ .label = "frame rate",
		      .value = std::format("{:.4g} fps", static_cast<double>(info.fps_num) / info.fps_den) }
		);

	if (info.pix_fmt)
		rows.push_back({ .label = "pixel format", .value = *info.pix_fmt });

	std::vector<std::string> colour;
	for (const auto& value : { info.color_space, info.color_transfer, info.color_primaries }) {
		// space, transfer and primaries are usually all the same (bt709), so only list each once
		if (value && !value->empty() && *value != "unknown" && std::ranges::find(colour, *value) == colour.end())
			colour.push_back(*value);
	}
	if (!colour.empty())
		rows.push_back({ .label = "colour", .value = u::join(colour, ", ") });

	if (info.color_range == "tv")
		rows.push_back({ .label = "range", .value = "limited" });
	else if (info.color_range == "pc")
		rows.push_back({ .label = "range", .value = "full" });

	if (include_audio) {
		const auto& rates = info.audio_sample_rates;

		std::string audio = "none";
		if (rates.size() == 1)
			audio = std::format("{} Hz", rates.front());
		else if (rates.size() > 1)
			audio = std::format("{} tracks, {} Hz", rates.size(), rates.front());

		rows.push_back({ .label = "audio", .value = audio });
	}

	return rows;
}
