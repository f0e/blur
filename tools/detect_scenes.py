"""Lists where blur's scene detection finds cuts in videos.

Run with lib/vapoursynth/python.exe, with VAPOURSYNTH_EXTRA_PLUGIN_PATH set to lib/vapoursynth/vs-plugins.
usage: python detect_scenes.py [--json] video [video ...]
"""

import argparse
import json
import sys
import time
from pathlib import Path

import vapoursynth as vs
from vapoursynth import core

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src" / "vapoursynth"))

from blur import scenes


def open_video(path: Path) -> vs.VideoNode:
    try:
        return core.lsmas.LWLibavSource(str(path), cache=0)
    except (AttributeError, vs.Error):
        return core.bs.VideoSource(str(path), cachemode=0)


def timestamp(seconds: float) -> str:
    minutes, seconds = divmod(seconds, 60)
    return f"{int(minutes)}:{seconds:06.3f}"


def detect(path: Path, as_json: bool):
    video = open_video(path)
    props = video.get_frame(0).props
    full_range = (props["_Range"] if "_Range" in props else props.get("_ColorRange")) == vs.RANGE_FULL
    cuts = scenes.detect(video, full_range)

    out = sys.stderr if as_json else sys.stdout
    print(
        f"\n{path.name} ({video.width}x{video.height}, {float(video.fps):g}fps, {video.num_frames} frames)",
        file=out,
    )

    found = 0
    started = time.time()
    step = max(1, video.num_frames // 20)

    for n, frame in enumerate(cuts.frames(close=True)):
        if frame.props[scenes.PROP_CUT]:
            found += 1
            score = float(frame.props[scenes.PROP_CHANGE]) / float(frame.props[scenes.PROP_MOTION])
            seconds = n / float(video.fps)

            if as_json:
                print(json.dumps({"frame": n, "time": seconds, "score": score}), flush=True)
            else:
                print(f"  cut at frame {n:>6} ({timestamp(seconds)}), {score:.1f}x the motion around it", flush=True)

        if n % step == 0:
            print(f"  {n / video.num_frames:.0%}", end="\r", file=sys.stderr, flush=True)

    elapsed = time.time() - started
    print(
        f"  {found} cut{'s' if found != 1 else ''} found in {elapsed:.1f}s ({video.num_frames / elapsed:.0f}fps)",
        file=out,
    )


def main():
    parser = argparse.ArgumentParser(description="List where blur's scene detection finds cuts in videos")
    parser.add_argument("videos", nargs="+", type=Path)
    parser.add_argument("--json", action="store_true", help="a line of json per cut, everything else on stderr")
    args = parser.parse_args()

    for path in args.videos:
        try:
            detect(path, args.json)
        except Exception as e:  # noqa: BLE001 - one bad video shouldn't stop the rest
            print(f"\n{path.name}: couldn't detect scenes ({e})", file=sys.stderr if args.json else sys.stdout)


if __name__ == "__main__":
    main()
