#include "media.h"

namespace {
	int get_video_preroll_frames(const std::filesystem::path& path, double fps, double max_preroll_seconds = 5.0) {
		// some mp4 files use an edit list to trim content by offsetting the start time, leaving negative pts
		// packets at the head of the file that normal players skip but some vapoursynth source plugins decode as real
		// frames, causing audio/video desync

		// this count those frames so we can trim the clip manually in vapoursynth if needed

		namespace bp = boost::process;
		bp::ipstream pipe_stream;

		const int max_packets = static_cast<int>(std::ceil(fps * max_preroll_seconds));
		const auto read_intervals = std::format("%+#{}", max_packets);

		auto c = u::run_command(
			blur.ffprobe_path,
			{
				"-v",
				"error",
				"-select_streams",
				"v:0",
				"-show_packets",
				"-read_intervals",
				read_intervals,
				"-show_entries",
				"packet=pts",
				"-of",
				"json",
				u::path_to_string(path),
			},
			bp::std_out > pipe_stream,
			bp::std_err.null()
		);

		if (!c) {
			u::log_error("failed to get preroll frames: {}", c.error());
			return 0;
		}

		std::string output(std::istreambuf_iterator<char>(pipe_stream), {});
		c->wait();

		const auto j = nlohmann::json::parse(output, nullptr, false);
		if (j.is_discarded()) {
			u::log_error("failed to parse preroll frames output: {}", output);
			return 0;
		}

		int skip = 0;
		for (const auto& pkt : j.value("packets", nlohmann::json::array())) {
			const auto pts = pkt.value("pts", 0LL);
			if (pts >= 0)
				break;
			skip++;
		}

		return skip;
	}
}

media::VideoInfo media::get_video_info(const std::filesystem::path& path) {
	namespace bp = boost::process;

	bp::ipstream pipe_stream;

	auto c = u::run_command(
		blur.ffprobe_path,
		{
			"-v",
			"error",
			"-show_entries",
			// clang-format off
			"stream=index,codec_type,sample_rate,color_range,r_frame_rate,pix_fmt,color_space,color_transfer,color_primaries,width,height,start_time,duration",
			// clang-format on
			"-show_entries",
			"format=duration,start_time",
			"-of",
			"json",
			u::path_to_string(path),
		},
		bp::std_out > pipe_stream,
		bp::std_err.null()
	);

	if (!c) {
		u::log_error("failed to get video info: {}", c.error());
		return {};
	}

	std::string output(std::istreambuf_iterator<char>(pipe_stream), {});

	c->wait();

	DEBUG_LOG("[ffprobe] {}", output);

	const auto j = nlohmann::json::parse(output, nullptr, false);
	if (j.is_discarded()) {
		u::log_error("failed to parse video info output: {}", output);
		return {};
	}

	VideoInfo info;

	auto opt_str = [](const nlohmann::json& stream, const std::string& key) -> std::optional<std::string> {
		if (!stream.contains(key))
			return std::nullopt;

		auto s = stream[key].get<std::string>();

		if (s.empty() || s == "unknown" || s == "reserved")
			return std::nullopt;

		return s;
	};

	// format
	if (j.contains("format")) {
		const auto& fmt = j["format"];

		if (fmt.contains("duration"))
			info.duration = std::stod(fmt["duration"].get<std::string>());

		if (fmt.contains("start_time"))
			info.start_time = std::stod(fmt["start_time"].get<std::string>());
	}

	// streams
	bool first_video_stream = false;

	for (const auto& stream : j.value("streams", nlohmann::json::array())) {
		const auto codec_type = stream.value("codec_type", "");

		if (codec_type == "video") {
			info.has_video_stream = true;

			if (first_video_stream)
				continue;

			first_video_stream = true;

			info.width = stream.value("width", 0);
			info.height = stream.value("height", 0);
			info.pix_fmt = opt_str(stream, "pix_fmt");
			info.color_range = opt_str(stream, "color_range");
			info.color_space = opt_str(stream, "color_space");
			info.color_transfer = opt_str(stream, "color_transfer");
			info.color_primaries = opt_str(stream, "color_primaries");

			if (stream.contains("r_frame_rate")) {
				const auto fps = u::split_string(stream["r_frame_rate"].get<std::string>(), "/");
				info.fps_num = std::stoi(fps[0]);
				info.fps_den = std::stoi(fps[1]);
			}

			if (stream.contains("start_time"))
				info.video_start_time = std::stod(stream["start_time"].get<std::string>());

			if (stream.contains("duration"))
				info.video_duration = std::stod(stream["duration"].get<std::string>());
		}
		else if (codec_type == "audio") {
			if (stream.contains("sample_rate"))
				info.audio_sample_rates.push_back(std::stoi(stream["sample_rate"].get<std::string>()));

			if (stream.contains("start_time"))
				info.audio_start_times.push_back(std::stod(stream["start_time"].get<std::string>()));
		}
	}

	// mkv doesn't store stream durations
	if (info.has_video_stream && info.video_duration <= 0.0)
		info.video_duration = info.duration - (info.video_start_time - info.start_time);

	info.preroll_frames = get_video_preroll_frames(path, (double)info.fps_num / info.fps_den);

	return info;
}

std::vector<uint8_t> media::get_video_frame_jpeg(const std::filesystem::path& path, float timestamp) {
	namespace bp = boost::process;

	bp::ipstream pipe_stream;

	auto c = u::run_command(
		blur.ffmpeg_path,
		{
			"-v",
			"error",
			// -ss before the input seeks from the nearest keyframe, which is what makes this fast enough to scrub with
			"-ss",
			std::format("{:.3f}", std::max(timestamp, 0.f)),
			"-i",
			u::path_to_string(path),
			"-map",
			"0:v:0",
			"-frames:v",
			"1",
			"-c:v",
			"mjpeg",
			"-q:v",
			"4",
			"-f",
			"image2pipe",
			"-",
		},
		bp::std_out > pipe_stream,
		bp::std_err.null()
	);

	if (!c) {
		u::log_error("failed to get video frame: {}", c.error());
		return {};
	}

	std::vector<uint8_t> jpeg;
	std::array<char, 4096> buffer{};

	while (pipe_stream.read(buffer.data(), buffer.size()) || pipe_stream.gcount() > 0) {
		jpeg.insert(jpeg.end(), buffer.data(), buffer.data() + pipe_stream.gcount());
	}

	c->wait();

	if (c->exit_code() != 0)
		return {};

	return jpeg;
}
