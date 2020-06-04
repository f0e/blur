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
	if (!std::filesystem::exists(path)) {
		// try create the path
		if (!std::filesystem::create_directory(path))
			return;
	}

	// set folder as hidden
	int attr = GetFileAttributesA(path.c_str());
	SetFileAttributesA(path.c_str(), attr | FILE_ATTRIBUTE_HIDDEN);
}

void c_avisynth_handler::remove_path() {
	// check if the path doesn\"t already exist
	if (!std::filesystem::exists(path))
		return;

	// remove the path and all the files inside
	std::filesystem::remove_all(path);
}

void c_avisynth_handler::create(std::string video_path, blur_settings& settings) {
	// create the avs file path
	create_path();

	// generate a random filename
	filename = path + random_string(6) + (".avs");

	// create the file output stream
	std::ofstream output(filename);

	// get timescale
	int true_fps = static_cast<int>(settings.input_fps * (1 / settings.timescale));
	int true_sound_rate = static_cast<int>((1 / settings.timescale) * 100);

	// multithreading
	output << fmt::format("SetFilterMTMode(\"DEFAULT_MT_MODE\", 2)") << "\n";
	output << fmt::format("SetFilterMTMode(\"InterFrame\", 3)") << "\n";
	
	// load video / audio source
	output << fmt::format("audio = FFAudioSource(\"{}\").TimeStretch(rate={})", video_path, true_sound_rate) << "\n";
	output << fmt::format("FFVideoSource(\"{}\", threads={}, fpsnum={}).AssumeFPS({})", video_path, settings.cpu_threads, settings.input_fps, true_fps) << "\n";
	
	output << fmt::format("ConvertToYV12()") << "\n";

	if (settings.blur) {
		// do frame blending

		int frame_gap, radius;

		if (settings.interpolate) {
			// interpolate footage, blur with the interpolated fps
			frame_gap = static_cast<int>(settings.interpolated_fps / settings.output_fps);
			radius = static_cast<int>((frame_gap * settings.exposure) / 2);

			output << fmt::format("InterFrame(NewNum={}, NewDen=1, Cores={}, Gpu=true, Tuning=\"Smooth\")", settings.interpolated_fps, settings.cpu_cores) << "\n";
		}
		else {
			// don't interpolate footage, just blur with the current fps
			frame_gap = static_cast<int>(true_fps / settings.output_fps);
			radius = static_cast<int>(frame_gap * settings.exposure);
		}

		// blur
		if (radius > 0)
			output << fmt::format("TemporalSoften({}, 255, 255, 0, 3)", radius) << "\n";

		if (frame_gap > 0)
			output << fmt::format("SelectEvery({}, 1)", frame_gap) << "\n";
	}
	else {
		// don't do frame blending

		if (settings.output_fps != true_fps) { // changing fps
			if (settings.output_fps > true_fps) { // need more fps
				// interpolate to output fps
				output << fmt::format("InterFrame(NewNum={}, NewDen=1, Cores={}, Gpu=true, Tuning=\"Smooth\")", settings.output_fps, settings.cpu_cores) << "\n";
			}
			else { // need less fps
				// set output fps
				output << fmt::format("ChangeFPS({})", settings.output_fps) << "\n";
			}
		}
	}

	// add audio to final video
	output << fmt::format("AudioDub(audio)") << "\n";

	// enable multithreading
	output << fmt::format("Prefetch({})", settings.cpu_threads) << "\n";

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