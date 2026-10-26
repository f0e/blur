import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

# blur's own ffmpeg, which is what it renders with
FFMPEG = shutil.which("ffmpeg", path=ROOT / "lib" / "ffmpeg") or shutil.which("ffmpeg")
FFPROBE = shutil.which("ffprobe", path=ROOT / "lib" / "ffmpeg") or shutil.which("ffprobe")


def run(cmd: list, cwd: Path | None = None) -> str:
    result = subprocess.run(
        [str(c) for c in cmd], check=False, cwd=cwd, capture_output=True, text=True, encoding="utf-8"
    )
    if result.returncode != 0:
        raise RuntimeError(f"{' '.join(map(str, cmd))}\n{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return result.stdout
