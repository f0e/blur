#include "includes.h"

void c_avisynth_handler::create(const std::string& temp_path, const std::string& video_path, const s_blur_settings& settings) {
	// generate a random filename
	filename = temp_path + helpers::random_string(6) + (".avs");

	// create the file output stream
	std::ofstream output(filename);

	// get timescale
	int true_fps = static_cast<int>(settings.input_fps * (1 / settings.input_timescale));
	int true_sound_rate = static_cast<int>((1 / settings.input_timescale) * 100);

	// multithreading
	output << "SetFilterMTMode(\"DEFAULT_MT_MODE\", MT_MULTI_INSTANCE)" << "\n";
	// output << fmt::format("SetFilterMTMode(\"InterFrame\", MT_SERIALIZED)") << "\n";
	// output << fmt::format("SetFilterMTMode(\"FFVideoSource\", MT_SERIALIZED)") << "\n";
	// output << fmt::format("SetFilterMTMode(\"DSS2\", MT_SERIALIZED)") << "\n";
	// output << fmt::format("SetFilterMTMode(\"AudioDub\", MT_SERIALIZED)") << "\n";
	
	// load video
	auto extension = std::filesystem::path(video_path).extension();
	if (extension != ".avi") {
		output << fmt::format("FFmpegSource2(\"{}\", fpsnum={}, atrack=-1, cache=false).AssumeFPS({})", video_path, settings.input_fps, true_fps) << "\n";
	}
	else {
		// FFmpegSource2 doesnt work with frameserver
		output << fmt::format("audio = FFAudioSource(\"{}\")", video_path) << "\n";
		output << fmt::format("DSS2(\"{}\", fps={}).AssumeFPS({})", video_path, settings.input_fps, true_fps) << "\n";
		output << fmt::format("AudioDub(audio)", video_path, settings.input_fps, true_fps) << "\n";
	}

	output << fmt::format("ConvertToYV12()") << "\n";

	// timestretch audio
	if (settings.input_timescale != 1.f) {
		output << fmt::format("TimeStretch(rate={})", true_sound_rate) << "\n";
	}

	int current_fps = true_fps;

	// interpolation
	if (settings.interpolate) {
		current_fps = settings.interpolated_fps; // store new fps

		std::string speed = settings.interpolation_speed;
		if (speed == "default") speed = "medium";

		std::string tuning = settings.interpolation_tuning;
		if (tuning == "default") tuning = "smooth";

		std::string algorithm = settings.interpolation_algorithm;
		if (algorithm == "default") algorithm = "13";

		output << fmt::format("InterFrame(NewNum={}, Cores={}, Gpu=true, Preset=\"{}\", Tuning=\"{}\", OverrideAlgo={})", settings.interpolated_fps, settings.cpu_cores, speed, tuning, algorithm) << "\n";
	}

	// timescale adjustment
	if (settings.output_timescale != 1.f) {
		current_fps = static_cast<int>(current_fps * settings.output_timescale);

		// adjust video
		output << fmt::format("AssumeFPS({}, sync_audio=true)", current_fps) << "\n";
	}

	// frame blending
	if (settings.blur) {
		int frame_gap = static_cast<int>(current_fps / settings.output_fps);
		int radius = static_cast<int>(frame_gap * settings.blur_amount);
		
		if (radius > 0)
			output << fmt::format("ClipBlend({})", radius) << "\n";

		if (frame_gap > 0)
			output << fmt::format("SelectEvery({}, 1)", frame_gap) << "\n";
	}

	if (settings.interpolate) {
		if (settings.multithreading) {
			// enable multithreading
			output << fmt::format("Prefetch()") << "\n";
		}
	}

	// set output fps
	output << fmt::format("ChangeFPS({}, LINEAR=false)", settings.output_fps) << "\n";

	// set file as hidden
	int attr = GetFileAttributesA(filename.c_str());
	SetFileAttributesA(filename.c_str(), attr | FILE_ATTRIBUTE_HIDDEN);
}