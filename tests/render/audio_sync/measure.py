import itertools
import json
import os
import statistics
from dataclasses import dataclass
from pathlib import Path

from blur_testing.tools import FFMPEG, FFPROBE, run


@dataclass
class Measurement:
    path: Path
    first_frame: float
    last_frame: float
    frame_interval: float
    flashes: list[float]  # brightness weighted middles, for blended frames
    flash_starts: list[float]
    onsets: list[list[float]]  # per audio track
    audio_spans: list[tuple[float, float]]

    @property
    def start(self) -> float:
        return min([self.first_frame, *(span[0] for span in self.audio_spans)])

    def flash_times(self, blended: bool) -> list[float]:
        return self.flashes if blended else self.flash_starts

    def offsets(self, blended: bool = False) -> list[list[float]]:
        """How far each tone is from its flash, per audio track."""
        flashes = self.flash_times(blended)
        return [[o - f for f, o in align(flashes, track)] if flashes else [] for track in self.onsets]


def measure(path: Path) -> Measurement:
    streams = json.loads(
        run([FFPROBE, "-v", "error", "-show_entries", "stream=index,codec_type:disposition", "-of", "json", path])
    )["streams"]
    # cover art is a video stream too
    video = next(
        s["index"] for s in streams if s["codec_type"] == "video" and not s.get("disposition", {}).get("attached_pic")
    )
    audio = [s["index"] for s in streams if s["codec_type"] == "audio"]

    # movie keeps the file's own timestamps, edit lists applied, rather than ffmpeg's shifted ones
    name = path.name.replace("'", r"\'")
    graph = f"movie='{name}':s={video},signalstats"
    entries = "frame=pts_time:frame_tags=lavfi.signalstats.YAVG"
    out = run(
        [FFPROBE, "-v", "error", "-f", "lavfi", "-i", graph, "-show_entries", entries, "-of", "json"], cwd=path.parent
    )
    frames = json.loads(out).get("frames", [])

    times = [float(f["pts_time"]) for f in frames]
    interval = statistics.median(b - a for a, b in itertools.pairwise(times)) if len(times) > 1 else 0.0
    lumas = [float(f["tags"]["lavfi.signalstats.YAVG"]) for f in frames]

    # blurred flashes spread over neighbouring frames, so each takes the brightness weighted middle
    background = statistics.median(lumas)
    peak = max(lumas) - background
    clusters = []
    for t, y in zip(times, lumas):
        excess = y - background
        if peak > 20 and excess > peak * 0.05:
            if not clusters or t - clusters[-1][-1][0] > 0.2:
                clusters.append([])
            clusters[-1].append((t, excess))

    flashes = [sum(t * e for t, e in c) / sum(e for _, e in c) for c in clusters]

    # a frame that's shown for longer (variable framerates) gets repeated when it's made constant, which moves
    # the weighted middle but not where it starts
    flash_starts = [next(t for t, e in c if e >= max(e for _, e in c) / 2) for c in clusters]

    onsets = []
    spans = []
    for index in audio:
        levels = audio_levels(path, index)
        onsets.append(tone_onsets(levels))
        spans.append((levels[0][0], levels[-1][0] + 0.001) if levels else (0.0, 0.0))

    return Measurement(
        path,
        times[0] if times else 0.0,
        times[-1] if times else 0.0,
        interval,
        flashes,
        flash_starts,
        onsets,
        spans,
    )


def audio_levels(path: Path, index: int) -> list[tuple[float, float]]:
    # tempo changes fade the tone in, so a silence threshold would fire early. the cli rather than amovie since mp2
    # changes sample format after the first frame
    levels = path.with_name(f"{path.name}.{index}.{os.getpid()}.levels")
    run(
        [
            *(FFMPEG, "-v", "error", "-y", "-copyts", "-i", path.name, "-map", f"0:{index}"),
            "-af",
            (
                "aresample=48000,asetnsamples=n=48:p=0,astats=metadata=1:reset=1,"
                f"ametadata=mode=print:key=lavfi.astats.Overall.RMS_level:file='{levels.name}'"
            ),
            *("-f", "null", "-"),
        ],
        cwd=path.parent,
    )

    out = []
    pts_time = None
    for line in levels.read_text().splitlines():
        if line.startswith("frame:"):
            pts_time = float(line.rsplit("pts_time:", 1)[1])
        elif line.startswith("lavfi.astats.Overall.RMS_level=") and pts_time is not None:
            out.append((pts_time, float(line.split("=", 1)[1])))
    levels.unlink()
    return out


def tone_onsets(levels: list[tuple[float, float]]) -> list[float]:
    if not levels:
        return []

    loud = max(level for _, level in levels)
    threshold = loud - 6
    quiet = loud - 26

    onsets = []
    quiet_since = None

    # the start of the stream counts as quiet, unless it starts partway through a tone
    armed = levels[0][1] < quiet
    for t, level in levels:
        if level < quiet:
            if quiet_since is None:
                quiet_since = t
            if t - quiet_since >= 0.1:
                armed = True
            continue

        quiet_since = None
        if armed and level >= threshold:
            onsets.append(t)
            armed = False
    return onsets


def align(flashes: list[float], onsets: list[float], tolerance=0.04) -> list[tuple[float, float]]:
    best, best_shift = [], 0.0
    for shift in {round(o - f, 4) for f, o in itertools.product(flashes, onsets)}:
        pairs = []
        for o in onsets:
            f = min(flashes, key=lambda f: abs(o - shift - f))
            if abs(o - shift - f) < tolerance:
                pairs.append((f, o))
        if len(pairs) > len(best) or (len(pairs) == len(best) and pairs and abs(shift) < abs(best_shift)):
            best, best_shift = pairs, shift
    return best
