import json
import sys
from pathlib import Path

import vapoursynth as vs
from vapoursynth import core

# add blur.py folder to path so it can reference scripts
sys.path.insert(1, str(Path(__file__).parent))

import blur.interpolate
import blur.utils as u

if vars().get("macos_bundled") == "true":
    u.load_plugins(".dylib")
elif vars().get("linux_bundled") == "true":
    u.load_plugins(".so")

list_type = vars().get("type", "")

video = core.std.BlankClip(width=1, height=1, length=2, fpsnum=1, fpsden=1, format=vs.RGBS)

match list_type:
    case "rife":
        core.rife.RIFE(video, list_gpu=True)

    case "tensorrt":

        def serialize_device(props: dict) -> dict:
            result = {}
            for k, v in props.items():
                if isinstance(v, bytes):
                    result[k] = v.decode("utf-8", errors="replace")
                else:
                    result[k] = v
            return result

        devices = []
        try:
            i = 0
            while True:
                device_props = core.trt.DeviceProperties(device_id=i)
                devices.append({"device_id": i, "properties": serialize_device(device_props)})
                i += 1
        except vs.Error as err:
            if "invalid device ordinal" in err.value:
                pass
            else:
                raise

        print(json.dumps(devices))

    case "svp":
        svp_video = core.std.BlankClip(width=256, height=256, length=4, fpsnum=4, fpsden=1, format=vs.YUV420P8)

        svp_video = blur.interpolate.SVP(
            svp_video,
            *blur.interpolate.generate_svp_strings(new_fps=8, gpu=True),
        )

        for n in range(svp_video.num_frames):
            svp_video.get_frame(n)

    case _:
        raise u.BlurException("Invalid list type")
