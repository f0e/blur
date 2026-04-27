import vapoursynth as vs
from vapoursynth import core

import sys
from pathlib import Path

# add blur.py folder to path so it can reference scripts
sys.path.insert(1, str(Path(__file__).parent))

import blur.interpolate
import blur.utils as u

if vars().get("macos_bundled") == "true":
    u.load_plugins(".dylib")
elif vars().get("linux_bundled") == "true":
    u.load_plugins(".so")

benchmark_type = vars().get("type", "")

device_index = vars().get("device_index")
if not device_index:
    raise u.BlurException("Device index not provided")

benchmark_video_path = vars().get("benchmark_video_path")
if benchmark_video_path is None or not Path(benchmark_video_path).exists():
    raise u.BlurException("Benchmark video not found")

if vars().get("enable_lsmash") == "true":
    video = core.lsmas.LWLibavSource(source=benchmark_video_path, cache=0)
else:
    video = core.bs.VideoSource(source=benchmark_video_path, cachemode=0)

video_info = u.VideoInfo(
    is_full_color_range=False,  # doesn't matter
    orig_width=video.width,
    orig_height=video.height,
    resize_chromaloc=None,  # doesn't matter
)

match benchmark_type:
    case "rife":
        model_path = vars().get("rife_model_path")
        if not model_path or not Path(model_path).exists():
            raise u.BlurException("RIFE model path not provided")

        video = blur.interpolate.interpolate_rife(
            video,
            video_info,
            new_fps=video.fps * 3,
            model_path=model_path,
            device_index=device_index,
        )
    case "rife (tensorrt)":
        model = vars().get("rife_trt_model")
        if not model:
            raise u.BlurException("RIFE model not provided")

        video = blur.interpolate.interpolate_rife_trt(
            video,
            video_info,
            new_fps=video.fps * 3,
            model=model,
            device_index=device_index,
        )

    case _:
        raise u.BlurException("Benchmark type invalid")

video.set_output()
