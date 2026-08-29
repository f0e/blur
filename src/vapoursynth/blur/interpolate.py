# credit to InterFrame - https://www.spirton.com/uploads/InterFrame/InterFrame2.html and https://github.com/HomeOfVapourSynthEvolution/havsfunc

import vapoursynth as vs
from vapoursynth import core

import json
import math
import sys
from fractions import Fraction
from typing import Any, Callable
from pathlib import Path

import blur.utils as u

if sys.platform in ("win32", "linux"):
    from external.vsmlrt import RIFE as VSMLRT_RIFE, BackendV2, RIFEModel
else:
    VSMLRT_RIFE = BackendV2 = RIFEModel = None

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
# here, and anything that asks for one is a failure waiting to happen: dedupe turns gpu off on purpose (see
# deduplicate.fill_drops_svp), and the gpu interpolation setting turns it off for the whole render
SVP_REQUIRES_GPU = sys.platform == "darwin"


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
):
    _video = core.fmtc.bitdepth(_video, bits=8)

    def process(video):
        return SVP(video, super_string, vectors_string, smooth_str)

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
        clip, super, bv, fv, num=new_fps, den=1, blend=blend, ml=max(masking, 1)
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
):
    u.check_model_path(model_path)

    def process(video):
        return RIFE(
            video,
            new_fps=new_fps,
            model_path=model_path,
            device_index=device_index,
        )

    return u.with_format(
        _video,
        video_info,
        vs.RGBS,
        process,
    )


def RIFE_vsmlrt(video: vs.VideoNode, new_fps: int, model: str, backend):
    multi_frac = Fraction(int(new_fps), int(video.fps))

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
        raise u.BlurException(
            "RIFE (TensorRT) is not supported on this platform."
        )

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
):
    return prepare_rife_vsmlrt(
        video=video,
        video_info=video_info,
        process_func=lambda _video, backend: RIFE_vsmlrt(
            _video, new_fps=new_fps, model=model, backend=backend
        ),
        backend_str="tensorrt",
        device_index=device_index,
        settings_path=settings_path,
    )
