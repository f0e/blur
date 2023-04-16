# credit to InterFrame - https://www.spirton.com/uploads/InterFrame/InterFrame2.html and https://github.com/HomeOfVapourSynthEvolution/havsfunc

import vapoursynth as vs
from vapoursynth import core

import json
import math

LEGACY_PRESETS = ["weak", "film", "smooth"]
NEW_PRESETS = ["default"]


def generate_svp_strings(
    new_fps,
    preset="weak",
    algorithm=13,
    masking=50,
    gpu=True
):
    # build super json
    super_json = {
        # "pel": 1,  # 1 = highest precision
        "gpu": gpu
    }

    # build vectors json
    vectors_json = {
        "block": {
            "w": 8,  # 8 = highest precision
            # "overlap": 0,  # 0 = highest precision
        }
    }

    if preset in LEGACY_PRESETS:
        vectors_json["main"] = {"search": {"distance": 0, "coarse": {}}}

        if preset == "weak":
            vectors_json["main"]["search"]["coarse"] = {
                "distance": -1,
                "trymany": True,
                "bad": {"sad": 2000},
            }
        else:
            vectors_json["main"]["search"]["coarse"] = {"distance": -10}

    # build smooth json
    smooth_json = {
        "rate": {
            "num": int(new_fps),
            "abs": True
        },
        "algo": algorithm,
        "mask": {
            # "cover":
            "area": masking,
            "area_sharp": 1.2,  # test if this does anything
        },
    }

    # if preset != "smooth":
    #     # test if this does anything
    #     smooth_json["mask"]["cover"] = 80

    if preset in LEGACY_PRESETS:
        smooth_json["scene"] = {
            "blend": True,
            "mode": 0
        }

        if preset == "weak":
            smooth_json["scene"]["limits"] = {
                "blocks": 50
            }

    return [json.dumps(obj) for obj in [super_json, vectors_json, smooth_json]]


def interpolate(
    video,
    new_fps,
    preset="weak",
    algorithm=13,
    masking=50,
    gpu=True,
):
    if not isinstance(video, vs.VideoNode):
        raise vs.Error("interpolate: input not a video")

    preset = preset.lower()

    print(
        f"preset: {preset}, algorithm: {algorithm}, masking: {masking}, gpu: {gpu}"
    )

    if preset not in LEGACY_PRESETS and preset not in NEW_PRESETS:
        raise vs.Error(f"interpolate: '{preset}' is not a valid preset")

    # generate svp strings
    [super_string, vectors_string, smooth_string] = generate_svp_strings(
        new_fps, preset, algorithm, masking, gpu)

    print(smooth_string)

    # interpolate
    super = core.svp1.Super(video, super_string)
    vectors = core.svp1.Analyse(
        super["clip"], super["data"], video, vectors_string)

    return core.svp2.SmoothFps(
        video,
        super["clip"],
        super["data"],
        vectors["clip"],
        vectors["data"],
        smooth_string,
    )


def change_fps(clip, fpsnum, fpsden=1):  # this is just directly from havsfunc
    if not isinstance(clip, vs.VideoNode):
        raise vs.Error('ChangeFPS: This is not a clip')

    factor = (fpsnum / fpsden) * (clip.fps_den / clip.fps_num)

    def frame_adjuster(n):
        real_n = math.floor(n / factor)
        one_frame_clip = clip[real_n] * (len(clip) + 100)
        return one_frame_clip

    attribute_clip = clip.std.BlankClip(length=math.floor(
        len(clip) * factor), fpsnum=fpsnum, fpsden=fpsden)
    return attribute_clip.std.FrameEval(eval=frame_adjuster)
