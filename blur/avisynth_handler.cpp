#include "avisynth_handler.h"

#include "includes.h"

c_avisynth_handler avisynth;

// generate a random string
std::string random_string(int len) {
	std::string str("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");

	std::random_device rd;
	std::mt19937 generator(rd());

	std::shuffle(str.begin(), str.end(), generator);

	return str.substr(0, len);
}

void c_avisynth_handler::create_path() {
	// check if the path already exists
	if (!std::filesystem::exists(avs_path)) {
		// try create the path
		if (!std::filesystem::create_directory(avs_path))
			return;
	}

	// set folder as hidden
	int attr = GetFileAttributesA(avs_path.c_str());
	SetFileAttributesA(avs_path.c_str(), attr | FILE_ATTRIBUTE_HIDDEN);
}

void c_avisynth_handler::remove_path() {
	// check if the path doesn\"t already exist
	if (!std::filesystem::exists(avs_path))
		return;

	// remove the path and all the files inside
	std::filesystem::remove_all(avs_path);
}

void c_avisynth_handler::create(std::string_view video_path, const blur_settings& settings) {
	// create the avs file path
	create_path();

	// generate a random filename
	filename = avs_path + random_string(6) + (".avs");

	// create the file output stream
	std::ofstream output(filename);

	// get timescale
	int true_fps = static_cast<int>(settings.input_fps * (1 / settings.timescale));
	int true_sound_rate = static_cast<int>((1 / settings.timescale) * 100);

	// multithreading
	output << fmt::format("SetFilterMTMode(\"DEFAULT_MT_MODE\", 2)") << "\n";
	output << fmt::format("SetFilterMTMode(\"InterFrame\", 3)") << "\n";
	output << fmt::format("SetFilterMTMode(\"FFVideoSource\", 3)") << "\n";

	// load video / audio source
	output << fmt::format("audio = FFAudioSource(\"{}\").TimeStretch(rate={})", video_path, true_sound_rate) << "\n";
	output << fmt::format("FFVideoSource(\"{}\", threads={}, fpsnum={}).AssumeFPS({})", video_path, settings.cpu_threads, settings.input_fps, true_fps) << "\n";
	
	output << fmt::format("ConvertToYV12()") << "\n";

	{
		int current_fps = true_fps;

		// interpolation
		if (settings.interpolate) {
			current_fps = settings.interpolated_fps; // store new fps

			std::string speed = settings.interpolation_speed;
			if (speed == "default") speed = "medium";

			std::string tuning = settings.interpolation_tuning;
			if (tuning == "default") tuning = "film";

			std::string algorithm = settings.interpolation_algorithm;
			if (algorithm == "default") algorithm = "13";

			output << fmt::format("InterFrame(NewNum={}, Cores={}, Gpu=true, Preset=\"{}\", Tuning=\"{}\", OverrideAlgo={})", settings.interpolated_fps, settings.cpu_cores, speed, tuning, algorithm) << "\n";
		}

		// frame blending
		if (settings.blur) {
			int frame_gap = static_cast<int>(current_fps / settings.output_fps);
			int radius = static_cast<int>((frame_gap * settings.blur_amount) / 2);

			if (radius > 0)
				output << fmt::format("TemporalSoften({}, 255, 255, 0, 3)", radius) << "\n";

			if (frame_gap > 0)
				output << fmt::format("SelectEvery({}, 1)", frame_gap) << "\n";
		}
	}

	// enable multithreading
	output << fmt::format("Prefetch({})", settings.cpu_threads) << "\n";

	// add audio to final video
	output << fmt::format("AudioDub(audio)") << "\n";

	// set output fps
	output << fmt::format("ChangeFPS({}, LINEAR=false)", settings.output_fps) << "\n";

	// set file as hidden
	int attr = GetFileAttributesA(filename.c_str());
	SetFileAttributesA(filename.c_str(), attr | FILE_ATTRIBUTE_HIDDEN);
}

void c_avisynth_handler::erase() {
	if (filename == "")
		return;

	// remove the path and files inside
	remove_path();
}