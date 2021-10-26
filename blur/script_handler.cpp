#include "includes.h"

void c_script_handler::create(const std::string& temp_path, const std::string& video_path, const s_blur_settings& settings) {
	// generate random filenames
	script_filename = temp_path + helpers::random_string(6) + ".vpy";

	// create video script
	std::ofstream video_script(script_filename);
	{
		video_script << "from vapoursynth import core" << "\n";
		video_script << "import vapoursynth as vs" << "\n";
		video_script << "import havsfunc as haf" << "\n";
		video_script << "import adjust" << "\n";
		video_script << "import weighting" << "\n";
		if (settings.deduplicate)
			video_script << "import filldrops" << "\n";

		if (helpers::to_lower(settings.interpolation_program) == "rife") {
			video_script << "from vsrife import RIFE" << "\n";
		}

		// load video
		std::string chungus = video_path;
		std::replace(chungus.begin(), chungus.end(), '\\', '/');

		auto extension = std::filesystem::path(video_path).extension();
		if (extension != ".avi") {
			video_script << fmt::format("video = core.ffms2.Source(source=\"{}\", cache=False)", chungus) << "\n";
		}
		else {
			// FFmpegSource2 doesnt work with frameserver
			video_script << fmt::format("video = core.avisource.AVISource(\"{}\")", chungus) << "\n";
			video_script << "video = core.fmtc.matrix(clip=video, mat=\"601\", col_fam=vs.YUV, bits=16)" << "\n";
			video_script << "video = core.fmtc.resample(clip=video, css=\"420\")" << "\n";
			video_script << "video = core.fmtc.bitdepth(clip=video, bits=8)" << "\n";
		}

		// replace duplicate frames with new frames which are interpolated based off of the surrounding frames
		if (settings.deduplicate)
			video_script << "video = filldrops.FillDrops(video, thresh=0.001)" << "\n";

		// input timescale
		if (settings.input_timescale != 1.f)
			video_script << fmt::format("video = core.std.AssumeFPS(video, fpsnum=(video.fps * (1 / {})))", settings.input_timescale) << "\n";

		// interpolation
		if (settings.interpolate) {
			if (helpers::to_lower(settings.interpolation_program) == "rife") {
				video_script << "video = core.resize.Bicubic(video, format=vs.RGBS)" << "\n";

				video_script << fmt::format("while video.fps < {}:", settings.interpolated_fps) << "\n";
				video_script << "	video = RIFE(video)" << "\n";

				video_script << "video = core.resize.Bicubic(video, format=vs.YUV420P8, matrix_s=\"709\")" << "\n";
			}
			else if (helpers::to_lower(settings.interpolation_program) == "rife-ncnn") {
				video_script << "video = core.resize.Bicubic(video, format=vs.RGBS)" << "\n";

				video_script << fmt::format("while video.fps < {}:", settings.interpolated_fps) << "\n";
				video_script << "	video = core.rife.RIFE(video)" << "\n";

				video_script << "video = core.resize.Bicubic(video, format=vs.YUV420P8, matrix_s=\"709\")" << "\n";
			}
			else {
				std::string speed = settings.interpolation_speed;
				if (helpers::to_lower(speed) == "default") speed = "medium";

				std::string tuning = settings.interpolation_tuning;
				if (helpers::to_lower(tuning) == "default") tuning = "smooth";

				std::string algorithm = settings.interpolation_algorithm;
				if (helpers::to_lower(algorithm) == "default") algorithm = "13";

				video_script << fmt::format("video = haf.InterFrame(video, GPU={}, NewNum={}, Preset=\"{}\", Tuning=\"{}\", OverrideAlgo={})", settings.gpu ? "True" : "False", settings.interpolated_fps, speed, tuning, algorithm) << "\n";
			}
		}

		// output timescale
		if (settings.output_timescale != 1.f)
			video_script << fmt::format("video = core.std.AssumeFPS(video, fpsnum=(video.fps * {}))", settings.output_timescale) << "\n";

		// blurring
		if (settings.blur) {
			video_script << fmt::format("frame_gap = int(video.fps / {})", settings.blur_output_fps) << "\n";
			video_script << fmt::format("blended_frames = int(frame_gap * {})", settings.blur_amount) << "\n";

			video_script << "if blended_frames > 0:" << "\n";
			
			// number of weights must be odd
			video_script << "	if blended_frames % 2 == 0:" << "\n";
			video_script << "		blended_frames += 1" << "\n";
			
			const std::unordered_map<std::string, std::function<std::string()>> weighting_functions = {
				{ "equal",			 [&]() { return fmt::format("weighting.equal(blended_frames)"); }},
				{ "gaussian",		 [&]() { return fmt::format("weighting.gaussian(blended_frames, {}, {})", settings.blur_weighting_gaussian_std_dev, settings.blur_weighting_bound); }},
				{ "gaussian_sym",	 [&]() { return fmt::format("weighting.gaussianSym(blended_frames, {}, {})", settings.blur_weighting_gaussian_std_dev, settings.blur_weighting_bound); }},
				{ "pyramid",		 [&]() { return fmt::format("weighting.pyramid(blended_frames, {})", settings.blur_weighting_triangle_reverse ? "True" : "False"); }},
				{ "pyramid_sym",	 [&]() { return "weighting.pyramid(blended_frames)"; }},
				{ "custom_weight",	 [&]() { return fmt::format("weighting.divide(blended_frames, {})", settings.blur_weighting); }},
				{ "custom_function", [&]() { return fmt::format("weighting.custom(blended_frames, '{}', {})", settings.blur_weighting, settings.blur_weighting_bound); }},
			};
			
			std::string weighting = settings.blur_weighting;
			if (!weighting_functions.contains(weighting)) {
				// check if it's a custom weighting function
				if (weighting.front() == '[' && weighting.back() == ']')
					weighting = "custom_weight";
				else
					weighting = "custom_function";
			}
			
			video_script << "	weights = " << weighting_functions.at(weighting)() << "\n";
			
			// frame blend
			// video_script << "	video = core.misc.AverageFrames(video, [1] * blended_frames)" << "\n";
			video_script << fmt::format("	video = core.frameblender.FrameBlend(video, weights, True)") << "\n";

			// video_script << "if frame_gap > 0:" << "\n";
			// video_script << "	video = core.std.SelectEvery(video, cycle=frame_gap, offsets=0)" << "\n";
			
			// set exact fps
			video_script << fmt::format("video = haf.ChangeFPS(video, {})", settings.blur_output_fps) << "\n";
		}

		// filters
		if (settings.brightness != 1.f || settings.contrast != 1.f || settings.saturation != 1.f)
			video_script << fmt::format("video = adjust.Tweak(video, bright={}, cont={}, sat={})", settings.brightness - 1.f, settings.contrast, settings.saturation) << "\n";

		video_script << "video.set_output()" << "\n";
	}
	video_script.close();

	// set scripts as hidden
	helpers::set_hidden(script_filename);
}