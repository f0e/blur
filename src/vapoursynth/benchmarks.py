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

BENCHMARK_WIDTH = 1920
BENCHMARK_HEIGHT = 1080
BENCHMARK_FPS = 24
BENCHMARK_LENGTH = 12


def generate_benchmark_video() -> vs.VideoNode:
    # chroma is subsampled, odd sizes/offsets aren't allowed
    def make_even(value: int) -> int:
        return value - (value % 2)

    box_width = make_even(BENCHMARK_WIDTH // 8)
    box_height = make_even(BENCHMARK_HEIGHT // 8)

    frames = []
    for i in range(BENCHMARK_LENGTH):
        progress = i / max(BENCHMARK_LENGTH - 1, 1)

        x = make_even(int((BENCHMARK_WIDTH - box_width) * progress))
        y = make_even(int((BENCHMARK_HEIGHT - box_height) * progress))

        box = core.std.BlankClip(
            width=box_width,
            height=box_height,
            format=vs.YUV420P8,
            length=1,
            fpsnum=BENCHMARK_FPS,
            fpsden=1,
            color=[235, 128, 128],
        )

        frames.append(
            core.std.AddBorders(
                box,
                left=x,
                right=BENCHMARK_WIDTH - box_width - x,
                top=y,
                bottom=BENCHMARK_HEIGHT - box_height - y,
                color=[16, 128, 128],
            )
        )

    return core.std.Splice(frames)


benchmark_type = vars().get("type", "")

device_index = vars().get("device_index")
if device_index is None:
    raise u.BlurException("Device index not provided")

device_index = int(device_index)  # vspipe args come through as strings

settings_path = Path(vars().get("settings_path", ""))

video = generate_benchmark_video()

video_info = u.VideoInfo(
    is_full_color_range=False,
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

        video = blur.interpolate.interpolate_rife_vsmlrt(
            video,
            video_info,
            new_fps=video.fps * 3,
            model=model,
            device_index=device_index,
            settings_path=settings_path,
        )

    case _:
        raise u.BlurException("Benchmark type invalid")

video.set_output()
