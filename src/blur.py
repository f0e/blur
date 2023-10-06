import vapoursynth as vs
from vapoursynth import core

# from vsrife import RIFE

import adjust
import blending
import deduplicate
import interpolate
import weighting

import json
from pathlib import Path

video_path = Path(vars().get("video_path"))
settings = json.loads(vars().get("settings"))

extension = video_path.suffix
if extension != ".avi":
    video = core.ffms2.Source(source=video_path, cache=False)
else:
    # FFmpegSource2 doesnt work with frameserver
    video = core.avisource.AVISource(video_path)
    video = core.fmtc.matrix(clip=video, mat="601", col_fam=vs.YUV, bits=16)
    video = core.fmtc.resample(clip=video, css="420")
    video = core.fmtc.bitdepth(clip=video, bits=8)

# replace duplicate frames with new frames which are interpolated based off of the surrounding frames
if settings["deduplicate"]:
    video = deduplicate.fill_drops_old(video, threshold=0.001)

# if settings["deduplicate"]:
# 	video = deduplicate.fill_drops(video, threshold=0.001, svp_preset=settings["interpolation_preset"],
#                                 svp_algorithm=settings["interpolation_algorithm"], svp_masking=settings["interpolation_mask_area"],
#                                 svp_gpu=settings["gpu_interpolation"])

# input timescale
if settings["input_timescale"] != 1:
    video = core.std.AssumeFPS(video, fpsnum=(video.fps * (1 / settings["input_timescale"])))

# interpolation
if settings["interpolate"]:
    # fix interpolated fps
    interpolated_fps = settings["interpolated_fps"]
    split = settings["interpolated_fps"].split("x")
    if len(split) > 1:
        # contains x, is a multiplier (e.g. 5x)
        interpolated_fps = video.fps * int(split[0])
    else:
        # no x, is an fps (e.g. 600)
        interpolated_fps = int(interpolated_fps)

    match settings["interpolation_program"].lower():
        case "rife":
            pass # TODO
            # video = core.resize.Bicubic(video, format=vs.RGBS)

            # while video.fps < interpolated_fps:
            #     video = RIFE(video)

            # video = core.resize.Bicubic(video, format=vs.YUV420P8, matrix_s="709")
            
        case "rife-ncnn":
            pass # TODO
            # video = core.resize.Bicubic(video, format=vs.RGBS)

            # while video.fps < interpolated_fps:
            #     video = core.rife.RIFE(video)

            # video = core.resize.Bicubic(video, format=vs.YUV420P8, matrix_s="709")
            
        case _:
            if settings["manual_svp"]:
                pass # TODO
                # todo
                # super = core.svp1.Super(video, settings["super_string"])
                # vectors = core.svp1.Analyse(super['clip'], super['data'], video, settings["vectors_string"])

                # # insert interpolated fps
                # fixed_smooth_string = settings["smooth_string"]
                # if "rate:" not in fixed_smooth_string:
                #     fixed_smooth_string.erase(0, 1);
                #     fixed_smooth_string.pop_back();
                #     fixed_smooth_string = "'{rate:{num:%d,abs:true}," + fixed_smooth_string + "}' % interpolated_fps";
                # }
                # else {
                #     # you're handling it i guess
                #     fixed_smooth_string = "'" + fixed_smooth_string + "'";
                # }

                # video_script << fmt::format("video = core.svp2.SmoothFps(video, super['clip'], super['data'], vectors['clip'], vectors['data'], {})", fixed_smooth_string) << "\n";
            else:
                print(f"new_fps={interpolated_fps}, preset={settings['interpolation_preset']}, algorithm={settings['interpolation_algorithm']}, blocksize={settings['interpolation_blocksize']}, overlap={0}, speed={settings['interpolation_speed']}, masking={settings['interpolation_mask_area']}, gpu={settings['gpu_interpolation']}")
                
                video = interpolate.interpolate_svp(video, new_fps=interpolated_fps, preset=settings["interpolation_preset"],
                                                    algorithm=settings["interpolation_algorithm"], blocksize=settings["interpolation_blocksize"],
                                                    overlap=0, speed=settings["interpolation_speed"], masking=settings["interpolation_mask_area"], gpu=settings["gpu_interpolation"])

# output timescale
if settings["output_timescale"] != 1:
    video = core.std.AssumeFPS(video, fpsnum=(video.fps * settings["output_timescale"]))

# blurring
if settings["blur"]:
    if settings["blur_amount"] > 0:
        frame_gap = int(video.fps / settings["blur_output_fps"])
        blended_frames = int(frame_gap * settings["blur_amount"])

        if blended_frames > 0:
            # number of weights must be odd
            if blended_frames % 2 == 0:
                blended_frames += 1

            def do_weighting_fn(blur_weighting_fn):
                match blur_weighting_fn:
                    case "gaussian":
                        return weighting.gaussian(blended_frames, settings["blur_weighting_gaussian_std_dev"], settings["blur_weighting_bound"])

                    case "gaussian_sym":
                        return weighting.gaussianSym(blended_frames, settings["blur_weighting_gaussian_std_dev"], settings["blur_weighting_bound"])

                    case "pyramid":
                        return weighting.pyramid(blended_frames, settings["blur_weighting_triangle_reverse"])

                    case "pyramid_sym":
                        return weighting.pyramidSym(blended_frames)

                    case "custom_weight":
                        return weighting.divide(blended_frames, settings["blur_weighting"])

                    case "custom_function":
                        return weighting.custom(blended_frames, settings["blur_weighting"], settings["blur_weighting_bound"])

                    case "equal":
                        return weighting.equal(blended_frames)
                    
                    case _:
                        # check if it's a custom weighting function
                        if blur_weighting_fn[0] == '[' and blur_weighting_fn[-1] == ']':
                            return do_weighting_fn("custom_weight")
                        else:
                            return do_weighting_fn("custom_function")

            weights = do_weighting_fn(settings["blur_weighting"])

            # frame blend
            # video = core.misc.AverageFrames(video, [1] * blended_frames)
            video = blending.average(video, weights)

    # if frame_gap > 0:
    #     video = core.std.SelectEvery(video, cycle=frame_gap, offsets=0)

    # set exact fps
    video = interpolate.change_fps(video, settings["blur_output_fps"])

# filters
if settings["brightness"] != 1 or settings["contrast"] != 1 or settings["saturation"] != 1:
    video = adjust.Tweak(video, bright=settings["brightness"] - 1, cont=settings["contrast"], sat=settings["saturation"])

video.set_output()