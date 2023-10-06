# credit to InterFrame - https://www.spirton.com/uploads/InterFrame/InterFrame2.html and https://github.com/HomeOfVapourSynthEvolution/havsfunc

import vapoursynth as vs
from vapoursynth import core

import json
import math

LEGACY_PRESETS = ["weak", "film", "smooth", "animation"]
NEW_PRESETS = ["default", "test"]


def generate_svp_strings(
    new_fps,
    preset="weak",
    algorithm=13,
    blocksize=8,
    overlap=2,
    speed="medium",
    masking=50,
    gpu=True,
):
    # build super json
    super_json = {
        "pel": 1,
        "gpu": gpu,
    }

    # build vectors json
    vectors_json = {
        "block": {
            "w": blocksize,
            "overlap": overlap,
        }
    }

    match preset:
        case "test":
            vectors_json["main"] = {
                "search": {"type": 3, "satd": True, "coarse": {"type": 3}}
            }
        case _ if preset in LEGACY_PRESETS:
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
        "rate": {"num": int(new_fps), "abs": True},
        "algo": algorithm,
        "mask": {
            "area": masking,
            "area_sharp": 1.2,  # test if this does anything
        },
        "scene": {
            "blend": False,
            "mode": 0,
            "limits": {"blocks": 9999999},  # dont want any scene detection stuff
        },
    }

    return [json.dumps(obj) for obj in [super_json, vectors_json, smooth_json]]


def interpolate_svp(
    video,
    new_fps,
    preset="weak",
    algorithm=13,
    blocksize=8,
    overlap=2,
    speed="medium",
    masking=50,
    gpu=True,
):
    if not isinstance(video, vs.VideoNode):
        raise vs.Error("interpolate: input not a video")

    preset = preset.lower()

    if preset not in LEGACY_PRESETS and preset not in NEW_PRESETS:
        raise vs.Error(f"interpolate: '{preset}' is not a valid preset")

    # generate svp strings
    [super_string, vectors_string, smooth_string] = generate_svp_strings(
        new_fps, preset, algorithm, blocksize, overlap, speed, masking, gpu
    )

    # interpolate
    super = core.svp1.Super(video, super_string)
    vectors = core.svp1.Analyse(super["clip"], super["data"], video, vectors_string)

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
        raise vs.Error("ChangeFPS: This is not a clip")

    factor = (fpsnum / fpsden) * (clip.fps_den / clip.fps_num)

    def frame_adjuster(n):
        real_n = math.floor(n / factor)
        one_frame_clip = clip[real_n] * (len(clip) + 100)
        return one_frame_clip

    attribute_clip = clip.std.BlankClip(
        length=math.floor(len(clip) * factor), fpsnum=fpsnum, fpsden=fpsden
    )
    return attribute_clip.std.FrameEval(eval=frame_adjuster)


def interpolate_mvtools(
    clip,
    fps=None,
    pel=1,
    sharp=0,
    blksize=4,
    overlap=2,
    search=5,
    searchparam=3,
    pelsearch=1,
    dct=3,
    blend=False,
    ml=200,
):
    if not isinstance(clip, vs.VideoNode) or clip.format.color_family not in [
        vs.GRAY,
        vs.YUV,
    ]:
        raise TypeError("JohnFPS: This is not a GRAY or YUV clip!")

    super = core.mv.Super(
        clip, hpad=blksize, vpad=blksize, pel=pel, rfilter=1, sharp=sharp
    )

    analyse_args = dict(
        blksize=blksize,
        overlap=overlap,
        search=search,
        searchparam=searchparam,
        pelsearch=pelsearch,
        dct=dct,
    )

    bv = core.mv.Analyse(super, isb=True, **analyse_args)
    fv = core.mv.Analyse(super, isb=False, **analyse_args)

    return core.mv.FlowFPS(clip, super, bv, fv, num=fps, den=1, blend=blend, ml=ml)


def JohnBlur(
    clip,
    num=None,
    den=None,
    pre=None,
    pel=None,
    sharp=2,
    blksize=16,
    overlap=8,
    blend=False,
    ml=200,
    analyse_args=None,
    recalculate_args=None,
):
    """
    From: https://forum.doom9.org/showthread.php?p=1847109.
    Motion Protected FPS converter script by johnmeyer.
    Slightly modified interface by Manolito, and a smidgen more by ssS.
    Args:
        num     (int) - Output framerate numerator.
        den     (int) - Output framerate denominator.
        pre    (clip) - pre-filtered clip used in motion vectors calculation.
        pel     (int) - accuracy of the motion estimation.
        sharp   (int) - subpixel interpolation method for pel > 1. (sharp = 3 → nnedi3)
        blksize (int) - blksize used in motion vectors calculation.
        overlap (int) - overlap used in motion vectors calculation.
        blend  (bool) - Whether to blend frames at scene change.
        ml      (int) - mask scale parameter. Greater values correspond to more weak occlusion mask.
    """

    isFLOAT = clip.format.sample_type == vs.FLOAT
    fn_RemoveGrain = core.rgsf.RemoveGrain if isFLOAT else core.rgvs.RemoveGrain
    fn_Analyse = core.mvsf.Analyse if isFLOAT else core.mv.Analyse
    fn_Super = core.mvsf.Super if isFLOAT else core.mv.Super
    fn_Recalculate = core.mvsf.Recalculate if isFLOAT else core.mv.Recalculate
    fn_FlowFPS = core.mvsf.FlowBlur if isFLOAT else core.mv.FlowBlur
    enum = clip.fps.numerator
    eden = clip.fps.denominator
    w = clip.width
    h = clip.height

    if not isinstance(clip, vs.VideoNode) or clip.format.color_family not in [
        vs.GRAY,
        vs.YUV,
    ]:
        raise TypeError("JohnFPS: This is not a GRAY or YUV clip!")

    if isinstance(num, float) or isinstance(den, float):
        raise ValueError("JohnFPS: Please use exact fraction instead of float.")

    if num is None and den is None:
        enum *= 2
    elif num is not None and den is None:
        enum = num
        eden = 1
    elif num is None and den is not None:
        raise ValueError("JohnFPS: denominator must be used with numerator.")
    else:
        enum = num
        eden = den

    if analyse_args is None:
        analyse_args = dict(
            blksize=blksize, overlap=overlap, search=5, searchparam=3, dct=5
        )

    if recalculate_args is None:
        recalculate_args = dict(
            blksize=blksize // 2, overlap=overlap // 2, search=5, dct=5, thsad=100
        )

    if pre is None:
        pre = fn_RemoveGrain(clip, [22])

    if pel is None:
        pel = 1 if w > 960 else 2

    if pel < 2:
        sharp = min(sharp, 2)

    ppp = pel > 1 and sharp > 2
    pre = DitherLumaRebuild(pre, 1)

    if ppp:
        cshift = 0.25 if pel == 2 else 0.375
        pclip = nnrs.nnedi3_resample(pre, w * pel, h * pel, cshift, cshift, nns=4)
        pclip2 = nnrs.nnedi3_resample(clip, w * pel, h * pel, cshift, cshift, nns=4)
        supero = fn_Super(
            clip,
            hpad=blksize,
            vpad=blksize,
            pel=pel,
            pelclip=pclip2,
            rfilter=1,
            levels=1,
        )
        superb = fn_Super(
            pre, hpad=blksize, vpad=blksize, pel=pel, pelclip=pclip, rfilter=4
        )
    else:
        supero = fn_Super(
            clip, hpad=blksize, vpad=blksize, pel=pel, rfilter=1, sharp=sharp, levels=1
        )
        superb = fn_Super(pre, hpad=blksize, vpad=blksize, pel=pel, rfilter=4, sharp=1)

    bv = fn_Analyse(superb, isb=True, **analyse_args)
    fv = fn_Analyse(superb, isb=False, **analyse_args)
    bv = fn_Recalculate(superb, bv, **recalculate_args)
    fv = fn_Recalculate(superb, fv, **recalculate_args)

    return fn_FlowFPS(clip, supero, bv, fv, 100, 1)
