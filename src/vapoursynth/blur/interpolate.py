# credit to InterFrame - https://www.spirton.com/uploads/InterFrame/InterFrame2.html and https://github.com/HomeOfVapourSynthEvolution/havsfunc

import vapoursynth as vs
from vapoursynth import core

import json
import math
import sys
from fractions import Fraction
from typing import Any, Callable
from pathlib import Path

import blur.deduplicate as deduplicate
import blur.utils as u

if sys.platform in ("win32", "linux"):
    from external.vsmlrt import (
        RIFE as VSMLRT_RIFE,
        RIFEMerge as VSMLRT_RIFE_MERGE,
        BackendV2,
        RIFEModel,
        bits_as,
    )
else:
    VSMLRT_RIFE = VSMLRT_RIFE_MERGE = BackendV2 = RIFEModel = bits_as = None

LEGACY_PRESETS = ["weak", "film", "smooth", "animation"]
NEW_PRESETS = ["default", "test"]

DEFAULT_PRESET = "weak"
DEFAULT_ALGORITHM = 13
DEFAULT_BLOCKSIZE = 8
DEFAULT_OVERLAP = 2
DEFAULT_SPEED = "medium"
DEFAULT_MASKING = 50
DEFAULT_GPU = True

# svpflow has no cpu renderer on apple silicon - SmoothFps refuses outright with "CPU rendering is not supported
# on ARM" - and the arm build is the only svp blur ships for macos. so there's no such thing as a cpu svp render
# here, and anything that asks for one is a failure waiting to happen - the gpu interpolation setting turns it
# off for the whole render
SVP_REQUIRES_GPU = sys.platform == "darwin"


# Retiming
#
# Deduplication doesn't hand interpolation a clip with the gaps already filled - see blur/deduplicate.py for
# why. It hands over a decision per frame instead, and interpolation renders straight onto the timeline those
# decisions describe: every output frame is generated from the two frames either side of it that were really
# captured, at the time point it falls between them.
#
# RIFE takes that time point directly, so `_retimed_rife_merge` asks it for exactly the frames wanted and
# nothing else. Everything else can only be asked for a framerate, so `_retimed` divides each gap into `steps`
# and picks the nearest one - which needs a clip laid out so that "the nearest step" is a frame that exists.

# how finely the gap between a pair of real frames is divided for an interpolator that only takes a framerate.
# an output frame is rounded to the nearest step, so this bounds how far from its true time it can land: eight
# steps to an output frame puts that under a tenth of a frame, which is well below anything visible
STEPS_PER_OUTPUT_FRAME = 8

# ...but not without limit. asking for more steps costs nothing to render - vapoursynth only ever works out
# the ones that get picked - but it does mean asking an interpolator for an absurd framerate, and they have
# their own opinions about that
MAX_STEPS = 256


def _fps(value) -> Fraction:
    """A framerate as an exact fraction, however it arrived - an int, or a float from an 'x' multiplier."""
    return Fraction(value).limit_denominator(1000000)


def _steps(ratio: Fraction, max_gap: int) -> int:
    return int(
        min(MAX_STEPS, max(2, math.ceil(ratio * max_gap * STEPS_PER_OUTPUT_FRAME)))
    )


def _retimed(
    video: vs.VideoNode,
    dedupe: deduplicate.Dedupe,
    dst_fps: Fraction,
    build: Callable[[vs.VideoNode, int], vs.VideoNode],
) -> vs.VideoNode:
    """Interpolate `video` to `dst_fps`, filling deduplication's gaps in the same pass.

    `build(clip, fps)` is the plain interpolation call for whichever method is in use, against a clip running
    at 1fps - so `fps` is a straight multiplier, and choosing it is this function's business rather than the
    caller's.

    What gets interpolated is a clip of *pairs*: frame 2n is source frame n, and frame 2n + 1 is the next
    frame after it that isn't a repeat of it. Interpolating that by `steps` puts `steps` frames between each
    pair, evenly spread across however long the gap between them really lasted - so the frame an output frame
    wants is the step nearest its own time, and picking it is all that's left to do. The odd numbered gaps in
    the pairs clip join frames that have nothing to do with each other, but nothing ever asks for a frame
    inside one, and vapoursynth doesn't render what nothing asks for.

    Pairs are indexed by the real frame they start from rather than by output frame, which is what stops a
    pair being interpolated again for every output frame that happens to fall inside it.
    """
    ratio = dst_fps / video.fps
    dst_frames = deduplicate.output_frames(dedupe.length, ratio)
    steps = _steps(ratio, dedupe.max_gap)

    following = core.std.FrameEval(
        video,
        lambda n, f: video[int(f.props[deduplicate.PROP_RIGHT])],
        prop_src=dedupe.decisions,
    )

    pairs = core.std.AssumeFPS(
        core.std.Interleave([video, following]), fpsnum=1, fpsden=1
    )

    generated = build(pairs, steps)
    last_generated = generated.num_frames - 1

    decisions = deduplicate.over_output(dedupe, dst_frames, ratio)

    def pick(n: int, f: vs.VideoFrame) -> vs.VideoNode:
        at = deduplicate.bracket(f.props, deduplicate.source_time(n, ratio))

        if at.timepoint is None:
            return video[at.left]

        step = min(round(at.timepoint * steps), steps)

        return generated[min((2 * at.left) * steps + step, last_generated)]

    out = core.std.FrameEval(
        core.std.BlankClip(video, length=dst_frames, keep=True),
        pick,
        prop_src=decisions,
    )

    return core.std.AssumeFPS(out, fpsnum=dst_fps.numerator, fpsden=dst_fps.denominator)


def _retimed_rife_merge(
    video: vs.VideoNode,
    dedupe: deduplicate.Dedupe,
    dst_fps: Fraction,
    merge: Callable[[vs.VideoNode, vs.VideoNode, vs.VideoNode], vs.VideoNode],
) -> vs.VideoNode:
    """`_retimed`, for an interpolator that can be asked for an exact time point.

    RIFE generates a frame from two frames and a number saying how far between them to land, so there's no
    rounding to a step and nothing generated that doesn't end up on screen: three clips - the frame before,
    the frame after, and the time point - describe the whole output, and one call renders it.
    """
    ratio = dst_fps / video.fps
    dst_frames = deduplicate.output_frames(dedupe.length, ratio)
    decisions = deduplicate.over_output(dedupe, dst_frames, ratio)

    base = core.std.BlankClip(video, length=dst_frames, keep=True)
    gray_format = vs.GRAYS if video.format.bits_per_sample == 32 else vs.GRAYH
    gray = core.std.BlankClip(base, format=gray_format, keep=True)

    def at(n: int, props) -> deduplicate.Bracket:
        return deduplicate.bracket(props, deduplicate.source_time(n, ratio))

    before = core.std.FrameEval(
        base, lambda n, f: video[at(n, f.props).left], prop_src=decisions
    )
    after = core.std.FrameEval(
        base, lambda n, f: video[at(n, f.props).right], prop_src=decisions
    )
    timepoint = core.std.FrameEval(
        gray,
        lambda n, f: gray.std.BlankClip(
            color=float(at(n, f.props).timepoint or 0), keep=True
        ),
        prop_src=decisions,
    )

    merged = merge(before, after, timepoint)

    # frames that land squarely on a real frame, or between two identical ones, don't go near the model
    held = bits_as(before, merged)

    def pick(n: int, f: vs.VideoFrame) -> vs.VideoNode:
        return held if at(n, f.props).timepoint is None else merged

    out = core.std.FrameEval(merged, pick, prop_src=decisions)

    return core.std.AssumeFPS(out, fpsnum=dst_fps.numerator, fpsden=dst_fps.denominator)


def _with_rate(smooth_str: str, fps: int) -> str:
    """`smooth_str` asking for `fps` instead of whatever framerate it was built around."""
    smooth_json = json.loads(smooth_str)
    smooth_json["rate"] = {"num": int(fps), "den": 1, "abs": True}

    return json.dumps(smooth_json)


def generate_svp_strings(
    new_fps,
    preset=DEFAULT_PRESET,
    algorithm=DEFAULT_ALGORITHM,
    blocksize=DEFAULT_BLOCKSIZE,
    overlap=DEFAULT_OVERLAP,
    speed=DEFAULT_SPEED,
    masking=DEFAULT_MASKING,
    gpu=DEFAULT_GPU,
    scene_detect=False,
):
    if SVP_REQUIRES_GPU:
        gpu = True

    # build super json
    super_json = {
        "pel": 1,
        "gpu": gpu,
    }

    # build vectors json
    vectors_json: dict = {
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
    }

    if not scene_detect:
        # dont want any scene detection stuff when normally blurring (i think?)
        smooth_json["scene"] = {
            "blend": False,
            "mode": 0,
            "limits": {"blocks": 9999999},
        }

    return [json.dumps(obj) for obj in [super_json, vectors_json, smooth_json]]


def SVP(
    video: vs.VideoNode,
    super_string: str,
    vectors_string: str,
    smooth_str: str,
):
    try:
        super = core.svp1.Super(video, super_string)
        vectors = core.svp1.Analyse(super["clip"], super["data"], video, vectors_string)
        return core.svp2.SmoothFps(
            video,
            super["clip"],
            super["data"],
            vectors["clip"],
            vectors["data"],
            smooth_str,
        )
    except vs.Error as e:
        raise u.BlurException(
            user_error="Failed to initialise SVP. Ensure your GPU drivers are up to date. You may need to disable 'gpu interpolation'.",
            original_exception=e,
        )


def svp(
    _video: vs.VideoNode,
    video_info: u.VideoInfo,
    super_string: str,
    vectors_string: str,
    smooth_str: str,
    dedupe: deduplicate.Dedupe | None = None,
    new_fps=None,
):
    """`new_fps` is only needed alongside `dedupe`.

    Retiming picks its own framerate to interpolate at, so the one in `smooth_str` gets replaced and the
    target has to be passed separately - which also means a hand written smooth string keeps everything about
    it that isn't the rate.
    """
    _video = core.fmtc.bitdepth(_video, bits=8)

    def process(video):
        if dedupe is None:
            return SVP(video, super_string, vectors_string, smooth_str)

        return _retimed(
            video,
            dedupe,
            _fps(new_fps),
            lambda pairs, steps: SVP(
                pairs, super_string, vectors_string, _with_rate(smooth_str, steps)
            ),
        )

    return u.with_scaled_luminance(
        _video,
        vs.YUV420P8,
        process,
    )


def interpolate_svp(
    video: vs.VideoNode,
    video_info: u.VideoInfo,
    new_fps: int,
    preset=DEFAULT_PRESET,
    algorithm=DEFAULT_ALGORITHM,
    blocksize=DEFAULT_BLOCKSIZE,
    overlap=DEFAULT_OVERLAP,
    speed=DEFAULT_SPEED,
    masking=DEFAULT_MASKING,
    gpu=DEFAULT_GPU,
    dedupe: deduplicate.Dedupe | None = None,
):
    preset = preset.lower()

    if preset not in LEGACY_PRESETS and preset not in NEW_PRESETS:
        raise vs.Error(f"interpolate: '{preset}' is not a valid preset")

    # generate svp strings
    [super_string, vectors_string, smooth_string] = generate_svp_strings(
        new_fps, preset, algorithm, blocksize, overlap, speed, masking, gpu
    )

    return svp(
        video,
        video_info,
        super_string,
        vectors_string,
        smooth_string,
        dedupe=dedupe,
        new_fps=new_fps,
    )


def change_fps(
    clip: vs.VideoNode, fpsnum, fpsden=1
) -> vs.VideoNode:  # https://github.com/Jaded-Encoding-Thaumaturgy/vs-jetpack
    src_num, src_den = clip.fps_num, clip.fps_den

    if (fpsnum, fpsden) == (src_num, src_den):
        return clip

    factor = (fpsnum / fpsden) * (src_den / src_num)

    new_fps_clip = clip.std.BlankClip(
        length=math.floor(clip.num_frames * factor), fpsnum=fpsnum, fpsden=fpsden
    )

    return new_fps_clip.std.FrameEval(lambda n: clip[round(n / factor)])


def MVTools(
    clip,
    new_fps,
    blocksize=DEFAULT_BLOCKSIZE,
    masking=100,
    pel=1,
    sharp=0,
    overlap=DEFAULT_OVERLAP,
    search=5,
    searchparam=3,
    pelsearch=1,
    dct=3,
    blend=False,
):
    super = core.mv.Super(
        clip, hpad=blocksize, vpad=blocksize, pel=pel, rfilter=1, sharp=sharp
    )

    analyse_args = dict(
        blksize=blocksize,
        overlap=overlap,
        search=search,
        searchparam=searchparam,
        pelsearch=pelsearch,
        dct=dct,
    )

    bv = core.mv.Analyse(super, isb=True, **analyse_args)
    fv = core.mv.Analyse(super, isb=False, **analyse_args)

    return core.mv.FlowFPS(
        clip, super, bv, fv, num=int(new_fps), den=1, blend=blend, ml=max(masking, 1)
    )


def interpolate_mvtools(
    clip,
    new_fps,
    blocksize=DEFAULT_BLOCKSIZE,
    masking=100,
    pel=1,
    sharp=0,
    overlap=DEFAULT_OVERLAP,
    search=5,
    searchparam=3,
    pelsearch=1,
    dct=3,
    blend=False,
    dedupe: deduplicate.Dedupe | None = None,
):
    settings = dict(
        blocksize=blocksize,
        masking=masking,
        pel=pel,
        sharp=sharp,
        overlap=overlap,
        search=search,
        searchparam=searchparam,
        pelsearch=pelsearch,
        dct=dct,
        blend=blend,
    )

    if dedupe is None:
        return MVTools(clip, new_fps, **settings)

    return _retimed(
        clip,
        dedupe,
        _fps(new_fps),
        lambda pairs, steps: MVTools(pairs, steps, **settings),
    )


def RIFE(video: vs.VideoNode, new_fps: int, model_path: str, device_index: int):
    try:
        return core.rife.RIFE(
            video,
            fps_num=new_fps,
            fps_den=1,
            model_path=model_path,
            gpu_id=device_index,
        )
    except vs.Error as e:
        raise u.BlurException(
            user_error="Failed to initialise RIFE. Ensure your 'rife gpu' is set correctly, and your GPU drivers are up to date. You may need to switch to a different interpolation method.",
            original_exception=e,
        )


def interpolate_rife(
    _video: vs.VideoNode,
    video_info: u.VideoInfo,
    new_fps: int,
    model_path: str,
    device_index: int,
    dedupe: deduplicate.Dedupe | None = None,
):
    u.check_model_path(model_path)

    def process(video):
        if dedupe is None:
            return RIFE(
                video,
                new_fps=new_fps,
                model_path=model_path,
                device_index=device_index,
            )

        return _retimed(
            video,
            dedupe,
            _fps(new_fps),
            lambda pairs, steps: RIFE(
                pairs,
                new_fps=steps,
                model_path=model_path,
                device_index=device_index,
            ),
        )

    return u.with_format(
        _video,
        video_info,
        vs.RGBS,
        process,
    )


def parse_rife_model(value: str) -> RIFEModel:
    RIFE_MODEL_MAP: dict[str, RIFEModel] = {
        member.name.replace("_", "."): member for member in RIFEModel
    }

    key = value.replace(".", "_")

    try:
        return RIFEModel[key]
    except KeyError:
        raise ValueError(
            f"Unknown RIFE model: {value!r}. Valid options: {list(RIFE_MODEL_MAP)}"
        )


def RIFE_vsmlrt(video: vs.VideoNode, new_fps: int, model: str, backend):
    multi_frac = Fraction(int(new_fps), int(video.fps))

    res = VSMLRT_RIFE(
        video,
        multi=multi_frac,
        model=parse_rife_model(model),
        ensemble=False,
        backend=backend,
        video_player=False,
        _implementation=2,
    )

    return res


def prepare_rife_vsmlrt(
    video: vs.VideoNode,
    video_info: u.VideoInfo,
    process_func: Callable[[vs.VideoNode, Any], vs.VideoNode],
    backend_str: str,
    device_index: int,
    settings_path: Path,
    override_format: str | None = None,
):
    if VSMLRT_RIFE is None:
        raise u.BlurException("RIFE (TensorRT) is not supported on this platform.")

    pad_mult: int | None = None
    target_format = vs.RGBH

    engine_folder = settings_path / "vsmlrt-engines"

    match backend_str:
        case "tensorrt":
            backend = BackendV2.TRT(
                num_streams=4,
                fp16=True,
                output_format=1,
                use_cuda_graph=True,
                engine_folder=engine_folder,
                device_id=device_index,
            )
            pad_mult = 64

        case "tensorrt rtx":
            backend = BackendV2.TRT_RTX(
                num_streams=4,
                fp16=True,
                use_cuda_graph=True,
                engine_folder=engine_folder,
                device_id=device_index,
            )
            pad_mult = 64

        case "vsort cuda":
            backend = BackendV2.ORT_CUDA(
                num_streams=4,
                fp16=True,
                device_id=device_index,
            )

        case "openvino cpu":
            backend = BackendV2.OV_CPU(num_streams=4, bf16=True)

        case "openvino gpu":
            backend = BackendV2.OV_GPU(
                num_streams=4,
                fp16=True,
                device_id=device_index,
            )

        case "ncnn":
            backend = BackendV2.NCNN_VK(
                num_streams=4,
                fp16=True,
                device_id=device_index,
            )

        case _:
            raise u.BlurException(f"Invalid RIFE backend: '{backend_str}'.")

    if override_format:
        target_format = override_format

    return u.with_format(
        video,
        video_info,
        target_format,
        lambda _video: u.with_padding(
            _video,
            multiple=pad_mult,
            process_func=lambda __video: process_func(__video, backend),
        ),
    )


def interpolate_rife_vsmlrt(
    video: vs.VideoNode,
    video_info: u.VideoInfo,
    new_fps: int,
    model: str,
    device_index: int,
    settings_path: Path,
    dedupe: deduplicate.Dedupe | None = None,
):
    def process(_video: vs.VideoNode, backend) -> vs.VideoNode:
        if dedupe is None:
            return RIFE_vsmlrt(_video, new_fps=new_fps, model=model, backend=backend)

        return _retimed_rife_merge(
            _video,
            dedupe,
            _fps(new_fps),
            lambda before, after, timepoint: VSMLRT_RIFE_MERGE(
                clipa=before,
                clipb=after,
                mask=timepoint,
                model=parse_rife_model(model),
                ensemble=False,
                backend=backend,
                _implementation=2,
            ),
        )

    return prepare_rife_vsmlrt(
        video=video,
        video_info=video_info,
        process_func=process,
        backend_str="tensorrt",
        device_index=device_index,
        settings_path=settings_path,
    )
