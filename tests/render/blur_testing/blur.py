import math
import os
import subprocess
from dataclasses import dataclass, field
from pathlib import Path


def _value(v) -> str:
    return str(v).lower() if isinstance(v, bool) else str(v)


@dataclass(frozen=True)
class Config:
    name: str
    blur: bool = False
    output_fps: int = 30
    timescale: float | None = None
    pitch: bool = False
    copy_audio: bool = False
    source_plugin: str = "LWLibavSource"
    gpu_decoding: bool = False
    gpu_encoding: bool = False
    fully_blur_first_frame: bool = False
    blur_amount: float = 1.0
    input_timescale: float = 1.0
    interpolated_fps: int | None = None
    deduplicate: bool = False
    container: str = "mp4"
    # any other settings, by their name in the config file
    extra: dict[str, object] = field(default_factory=dict, hash=False)

    @property
    def speed(self) -> float:
        return (self.timescale or 1.0) / self.input_timescale

    @property
    def skipped_time(self) -> float:
        # 'fully blur first frame' drops the output frames whose blur would reach before the video
        if not (self.blur and self.fully_blur_first_frame):
            return 0.0
        return math.ceil(self.blur_amount / 2) / self.output_fps

    def text(self) -> str:
        settings = {
            "blur": self.blur,
            "blur amount": self.blur_amount,
            "blur output fps": self.output_fps,
            "blur weighting": "equal",
            "interpolate": self.interpolated_fps is not None,
            "interpolated fps": self.interpolated_fps or 1200,
            "interpolation method": "svp",
            "svp hud mask": False,
            "pre-interpolate": False,
            "deduplicate": self.deduplicate,
            "use frame timing logs": False,
            "scene detection": False,
            "mask": "",
            "auto mask": False,
            "encode preset": "h264",
            "quality": 18,
            "preview": False,
            "detailed filenames": False,
            "gpu decoding": self.gpu_decoding,
            "gpu encoding": self.gpu_encoding,
            "timescale": self.timescale is not None or self.input_timescale != 1,
            "input timescale": self.input_timescale,
            "output timescale": self.timescale or 1,
            "adjust timescaled audio pitch": self.pitch,
            "filters": False,
            "advanced": True,
            "video container": self.container,
            "custom ffmpeg filters": "-c:v libx264 -preset ultrafast -crf 20 -c:a copy" if self.copy_audio else "",
            "source plugin": self.source_plugin,
            **self.extra,
        }
        return "".join(f"{k}: {_value(v)}\n".replace(": \n", ":\n") for k, v in settings.items())


@dataclass
class Blur:
    cli: Path
    appdata: Path

    def render(self, config: Config, source: Path, out: Path, start: float | None = None, end: float | None = None):
        config_path = out.with_suffix(".cfg")
        config_path.parent.mkdir(parents=True, exist_ok=True)
        config_path.write_text(config.text())

        # app settings live in appdata, so each config gets its own
        appdata = self.appdata / config.name
        app_config = appdata / "blur" / "app.cfg"
        app_config.parent.mkdir(parents=True, exist_ok=True)
        app_config.write_text(
            f"fully blur first frame: {_value(config.fully_blur_first_frame)}\n"
            "blur amount tied to fps: false\ncheck for updates: false\ngpu type (nvidia/amd/intel): nvidia\n"
        )

        cmd = [self.cli, "-i", source, "-o", out, "-c", config_path]
        if start is not None:
            cmd += ["--start", start]
        if end is not None:
            cmd += ["--end", end]

        out.unlink(missing_ok=True)
        result = subprocess.run(
            [str(c) for c in cmd],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            env=os.environ | {"APPDATA": str(appdata)},
        )
        if result.returncode != 0 or not out.exists():
            raise RuntimeError(f"blur-cli failed ({result.returncode})\n{(result.stdout + result.stderr)[-800:]}")
