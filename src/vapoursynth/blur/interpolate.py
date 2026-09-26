# credit to InterFrame - https://www.spirton.com/uploads/InterFrame/InterFrame2.html and https://github.com/HomeOfVapourSynthEvolution/havsfunc

import json
import math
import sys
from collections.abc import Callable
from fractions import Fraction
from pathlib import Path
from typing import Any

import blur.utils as u
import vapoursynth as vs
from blur import retime
from vapoursynth import core

TENSORRT_NOT_INSTALLED = (
    'TensorRT RIFE isn\'t installed. Rerun the installer and select "NVIDIA TensorRT RIFE interpolation", '
    "or use a different interpolation method"
)


def _vsmlrt():
    if sys.platform not in ("win32", "linux"):
        raise u.BlurException("RIFE (TensorRT) is not supported on this platform.")

    try:
        from external import vsmlrt
    except RuntimeError:
        raise u.BlurException(TENSORRT_NOT_INSTALLED)

    return vsmlrt


LEGACY_PRESETS = ["weak", "film", "smooth", "animation"]
NEW_PRESETS = ["default", "test"]

DEFAULT_PRESET = "weak"
DEFAULT_ALGORITHM = 13
DEFAULT_BLOCKSIZE = 8
DEFAULT_OVERLAP = 2
DEFAULT_SPEED = "medium"
DEFAULT_MASKING = 50
DEFAULT_GPU = True

# svpflow has no cpu renderer on apple silicon
SVP_REQUIRES_GPU = sys.platform == "darwin"


# Retiming
#
# rife takes a time point directly (`_retimed_rife_merge`). everything else only takes a framerate, so `_retimed`
# divides each gap into steps and picks the nearest one

# steps per output frame for interpolators that only take a framerate. 8 keeps rounding under a tenth of a frame
STEPS_PER_OUTPUT_FRAME = 8

# unused steps aren't rendered, but interpolators don't like absurd framerates
MAX_STEPS = 256


def _fps(value) -> Fraction:
    """A framerate as an exact fraction."""
    return Fraction(value).limit_denominator(1000000)


def _steps(ratio: Fraction, max_gap: int) -> int:
    return int(min(MAX_STEPS, max(2, math.ceil(ratio * max_gap * STEPS_PER_OUTPUT_FRAME))))


def _retimed(
    video: vs.VideoNode,
    timeline: retime.Timeline,
    dst_fps: Fraction,
    build: Callable[[vs.VideoNode, int], vs.VideoNode],
) -> vs.VideoNode:
    """Interpolate `video` to `dst_fps`, filling the timeline's gaps in the same pass. `build(clip, fps)` is the
    interpolation call, run on a 1fps clip so `fps` is a multiplier.

    Interpolates a clip of pairs (frame 2j is the real frame before decision j, 2j + 1 the one after) by `steps`,
    then each output frame picks its nearest step. Frames between unrelated pairs are never requested so never
    rendered.
    """
    ratio = dst_fps / video.fps
    dst_frames = retime.output_frames(timeline.length, ratio)
    steps = _steps(ratio, timeline.max_gap)

    decisions = timeline.decisions
    sides = core.std.BlankClip(video, length=decisions.num_frames, keep=True)

    def side(index: int) -> vs.VideoNode:
        return core.std.FrameEval(
            sides,
            lambda n, f: video[retime.frame_at(f.props, index)],
            prop_src=decisions,
        )

    # `slots` pairs per decision, whether or not they're all used
    pairs = core.std.AssumeFPS(
        core.std.Interleave([side(slot + end) for slot in range(timeline.slots) for end in (0, 1)]),
        fpsnum=1,
        fpsden=1,
    )

    generated = build(pairs, steps)
    last_generated = generated.num_frames - 1

    output_decisions = retime.over_output(timeline, dst_frames, ratio)

    def pick(n: int, f: vs.VideoFrame) -> vs.VideoNode:
        at = retime.bracket(f.props, retime.source_time(n, ratio))

        if at.timepoint is None:
            return video[at.left]

        step = min(round(at.timepoint * steps), steps)
        pair = retime.decision_index(timeline, n, ratio) * timeline.slots + at.slot

        return generated[min((2 * pair) * steps + step, last_generated)]

    out = core.std.FrameEval(
        core.std.BlankClip(video, length=dst_frames, keep=True),
        pick,
        prop_src=output_decisions,
    )

    return core.std.AssumeFPS(out, fpsnum=dst_fps.numerator, fpsden=dst_fps.denominator)


def _retimed_rife_merge(
    video: vs.VideoNode,
    timeline: retime.Timeline,
    dst_fps: Fraction,
    merge: Callable[[vs.VideoNode, vs.VideoNode, vs.VideoNode], vs.VideoNode],
) -> vs.VideoNode:
    """`_retimed`, for an interpolator that can be asked for an exact time point (rife)."""
    ratio = dst_fps / video.fps
    dst_frames = retime.output_frames(timeline.length, ratio)
    decisions = retime.over_output(timeline, dst_frames, ratio)

    base = core.std.BlankClip(video, length=dst_frames, keep=True)
    gray_format = vs.GRAYS if video.format.bits_per_sample == 32 else vs.GRAYH
    gray = core.std.BlankClip(base, format=gray_format, keep=True)

    def at(n: int, props) -> retime.Bracket:
        return retime.bracket(props, retime.source_time(n, ratio))

    before = core.std.FrameEval(base, lambda n, f: video[at(n, f.props).left], prop_src=decisions)
    after = core.std.FrameEval(base, lambda n, f: video[at(n, f.props).right], prop_src=decisions)
    timepoint = core.std.FrameEval(
        gray,
        lambda n, f: gray.std.BlankClip(color=float(at(n, f.props).timepoint or 0), keep=True),
        prop_src=decisions,
    )

    merged = merge(before, after, timepoint)

    # frames landing on a real frame skip the model
    held = _vsmlrt().bits_as(before, merged)

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
            vectors_json["main"] = {"search": {"type": 3, "satd": True, "coarse": {"type": 3}}}
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
    timeline: retime.Timeline | None = None,
    new_fps=None,
):
    """`new_fps` is only needed alongside `timeline`, since retiming replaces the rate in `smooth_str`."""
    _video = core.fmtc.bitdepth(_video, bits=8)

    def process(video):
        if timeline is None:
            # SmoothFps stops one source frame short, so pad with a repeat of the last frame and trim back
            smooth = SVP(video + video[-1], super_string, vectors_string, smooth_str)
            return smooth[: int(video.num_frames * smooth.fps / video.fps)]

        return _retimed(
            video,
            timeline,
            _fps(new_fps),
            lambda pairs, steps: SVP(pairs, super_string, vectors_string, _with_rate(smooth_str, steps)),
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
    timeline: retime.Timeline | None = None,
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
        timeline=timeline,
        new_fps=new_fps,
    )


def change_fps(
    clip: vs.VideoNode, fpsnum, fpsden=1
) -> vs.VideoNode:  # https://github.com/Jaded-Encoding-Thaumaturgy/vs-jetpack
    src_num, src_den = clip.fps_num, clip.fps_den

    if (fpsnum, fpsden) == (src_num, src_den):
        return clip

    factor = (fpsnum / fpsden) * (src_den / src_num)

    new_fps_clip = clip.std.BlankClip(length=math.floor(clip.num_frames * factor), fpsnum=fpsnum, fpsden=fpsden)

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
    super = core.mv.Super(clip, hpad=blocksize, vpad=blocksize, pel=pel, rfilter=1, sharp=sharp)

    analyse_args = {
        "blksize": blocksize,
        "overlap": overlap,
        "search": search,
        "searchparam": searchparam,
        "pelsearch": pelsearch,
        "dct": dct,
    }

    bv = core.mv.Analyse(super, isb=True, **analyse_args)
    fv = core.mv.Analyse(super, isb=False, **analyse_args)

    return core.mv.FlowFPS(clip, super, bv, fv, num=int(new_fps), den=1, blend=blend, ml=max(masking, 1))


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
    timeline: retime.Timeline | None = None,
):
    settings = {
        "blocksize": blocksize,
        "masking": masking,
        "pel": pel,
        "sharp": sharp,
        "overlap": overlap,
        "search": search,
        "searchparam": searchparam,
        "pelsearch": pelsearch,
        "dct": dct,
        "blend": blend,
    }

    if timeline is None:
        return MVTools(clip, new_fps, **settings)

    return _retimed(
        clip,
        timeline,
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
    timeline: retime.Timeline | None = None,
):
    u.check_model_path(model_path)

    def process(video):
        if timeline is None:
            return RIFE(
                video,
                new_fps=new_fps,
                model_path=model_path,
                device_index=device_index,
            )

        return _retimed(
            video,
            timeline,
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


def RIFE_vsmlrt(video: vs.VideoNode, new_fps: int, model_path: str, backend):
    multi_frac = Fraction(int(new_fps), int(video.fps))

    res = _vsmlrt().RIFE(
        video,
        multi=multi_frac,
        model_path=model_path,
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
    vsmlrt = _vsmlrt()

    pad_mult: int | None = None
    target_format = vs.RGBH

    engine_folder = settings_path / "vsmlrt-engines"

    if device_index < 0 and backend_str != "openvino cpu":
        raise u.BlurException(f"No GPU was found for RIFE ({backend_str}). Make sure your GPU drivers are up to date.")

    match backend_str:
        case "tensorrt":
            # vsmlrt imports fine without the trt plugin
            if not hasattr(core, "trt"):
                raise u.BlurException(TENSORRT_NOT_INSTALLED)

            backend = vsmlrt.BackendV2.TRT(
                num_streams=4,
                fp16=True,
                output_format=1,
                use_cuda_graph=True,
                engine_folder=engine_folder,
                device_id=device_index,
            )
            pad_mult = 64

        case "tensorrt rtx":
            backend = vsmlrt.BackendV2.TRT_RTX(
                num_streams=4,
                fp16=True,
                use_cuda_graph=True,
                engine_folder=engine_folder,
                device_id=device_index,
            )
            pad_mult = 64

        case "vsort cuda":
            backend = vsmlrt.BackendV2.ORT_CUDA(
                num_streams=4,
                fp16=True,
                device_id=device_index,
            )

        case "openvino cpu":
            backend = vsmlrt.BackendV2.OV_CPU(num_streams=4, bf16=True)

        case "openvino gpu":
            backend = vsmlrt.BackendV2.OV_GPU(
                num_streams=4,
                fp16=True,
                device_id=device_index,
            )

        case "ncnn":
            backend = vsmlrt.BackendV2.NCNN_VK(
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
    model_path: str,
    device_index: int,
    settings_path: Path,
    timeline: retime.Timeline | None = None,
):
    u.check_model_path(model_path)

    def process(_video: vs.VideoNode, backend) -> vs.VideoNode:
        if timeline is None:
            return RIFE_vsmlrt(_video, new_fps=new_fps, model_path=model_path, backend=backend)

        return _retimed_rife_merge(
            _video,
            timeline,
            _fps(new_fps),
            lambda before, after, timepoint: _vsmlrt().RIFEMerge(
                clipa=before,
                clipb=after,
                mask=timepoint,
                model_path=model_path,
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
