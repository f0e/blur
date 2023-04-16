#include "includes.h"

void c_script_handler::create(const std::filesystem::path& temp_path, const std::filesystem::path& video_path, const s_blur_settings& settings) {
	// generate random filenames
	script_path = temp_path / (helpers::random_string(6) + ".vpy");

	// create video script
	std::ofstream video_script(script_path);
	{
		video_script << "from vapoursynth import core" << "\n";
		video_script << "import vapoursynth as vs" << "\n";
		video_script << "import interpolate" << "\n";
		video_script << "import adjust" << "\n";
		video_script << "import weighting" << "\n";

		if (settings.deduplicate)
			video_script << "import deduplicate" << "\n";

		if (helpers::to_lower(settings.interpolation_program) == "rife")
			video_script << "from vsrife import RIFE" << "\n";

		// load video
		std::string path_string = video_path.string();
		std::replace(path_string.begin(), path_string.end(), '\\', '/');

		auto extension = std::filesystem::path(video_path).extension();
		if (extension != ".avi") {
			video_script << fmt::format("video = core.ffms2.Source(source=\"{}\", cache=False)", path_string) << "\n";
		}
		else {
			// FFmpegSource2 doesnt work with frameserver
			video_script << fmt::format("video = core.avisource.AVISource(\"{}\")", path_string) << "\n";
			video_script << "video = core.fmtc.matrix(clip=video, mat=\"601\", col_fam=vs.YUV, bits=16)" << "\n";
			video_script << "video = core.fmtc.resample(clip=video, css=\"420\")" << "\n";
			video_script << "video = core.fmtc.bitdepth(clip=video, bits=8)" << "\n";
		}

		// replace duplicate frames with new frames which are interpolated based off of the surrounding frames
		if (settings.deduplicate)
			video_script << "video = deduplicate.fill_drops_old(video, threshold=0.001)" << "\n";

		// if (settings.deduplicate)
		// 	video_script << fmt::format("video = deduplicate.fill_drops(video, threshold=0.001, svp_preset='{}', svp_algorithm={}, svp_masking={}, svp_gpu={})",
		// 		settings.interpolation_preset,
		// 		settings.interpolation_algorithm,
		// 		settings.interpolation_mask_area,
		// 		settings.gpu_interpolation ? "True" : "False"
		// 	) << "\n";

		// input timescale
		if (settings.input_timescale != 1.f)
			video_script << fmt::format("video = core.std.AssumeFPS(video, fpsnum=(video.fps * (1 / {})))", settings.input_timescale) << "\n";

		// interpolation
		if (settings.interpolate) {
			std::string actual_interpolated_fps = settings.interpolated_fps;
			auto split = helpers::split_string(settings.interpolated_fps, "x");
			if (split.size() > 1) {
				// contains x, is a multiplier
				actual_interpolated_fps = fmt::format("int(video.fps * {})", split[0]);
			}

			video_script << fmt::format("interpolated_fps = {}", actual_interpolated_fps) << "\n";

			if (helpers::to_lower(settings.interpolation_program) == "rife") {
				video_script << "video = core.resize.Bicubic(video, format=vs.RGBS)" << "\n";

				video_script << "while video.fps < interpolated_fps:" << "\n";
				video_script << "	video = RIFE(video)" << "\n";

				video_script << "video = core.resize.Bicubic(video, format=vs.YUV420P8, matrix_s=\"709\")" << "\n";
			}
			else if (helpers::to_lower(settings.interpolation_program) == "rife-ncnn") {
				video_script << "video = core.resize.Bicubic(video, format=vs.RGBS)" << "\n";

				video_script << "while video.fps < interpolated_fps:" << "\n";
				video_script << "	video = core.rife.RIFE(video)" << "\n";

				video_script << "video = core.resize.Bicubic(video, format=vs.YUV420P8, matrix_s=\"709\")" << "\n";
			}
			else {
				if (settings.manual_svp) {
					video_script << fmt::format("super = core.svp1.Super(video, '{}')", settings.super_string) << "\n";
					video_script << fmt::format("vectors = core.svp1.Analyse(super['clip'], super['data'], video, '{}')", settings.vectors_string) << "\n";

					// insert interpolated fps
					std::string fixed_smooth_string = settings.smooth_string;
					if (fixed_smooth_string.find("rate:") == std::string::npos) {
						fixed_smooth_string.erase(0, 1);
						fixed_smooth_string.pop_back();
						fixed_smooth_string = "'{rate:{num:%d,abs:true}," + fixed_smooth_string + "}' % interpolated_fps";
					}
					else {
						// you're handling it i guess
						fixed_smooth_string = "'" + fixed_smooth_string + "'";
					}

					video_script << fmt::format("video = core.svp2.SmoothFps(video, super['clip'], super['data'], vectors['clip'], vectors['data'], {})", fixed_smooth_string) << "\n";
				}
				else {
					video_script << fmt::format("video = interpolate.interpolate(video, new_fps=interpolated_fps, preset='{}', algorithm={}, masking={}, gpu={})",
						settings.interpolation_preset,
						settings.interpolation_algorithm,
						settings.interpolation_mask_area,
						settings.gpu_interpolation ? "True" : "False"
					) << "\n";
				}
			}
		}

		// output timescale
		if (settings.output_timescale != 1.f)
			video_script << fmt::format("video = core.std.AssumeFPS(video, fpsnum=(video.fps * {}))", settings.output_timescale) << "\n";

		// blurring
		if (settings.blur) {
			if (settings.blur_amount > 0) {
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
			}

			// video_script << "if frame_gap > 0:" << "\n";
			// video_script << "	video = core.std.SelectEvery(video, cycle=frame_gap, offsets=0)" << "\n";

			// set exact fps
			video_script << fmt::format("video = interpolate.change_fps(video, {})", settings.blur_output_fps) << "\n";
		}

		// filters
		if (settings.brightness != 1.f || settings.contrast != 1.f || settings.saturation != 1.f)
			video_script << fmt::format("video = adjust.Tweak(video, bright={}, cont={}, sat={})", settings.brightness - 1.f, settings.contrast, settings.saturation) << "\n";

		video_script << "video.set_output()" << "\n";
	}
	video_script.close();

	// set scripts as hidden
	helpers::set_hidden(script_path);
}