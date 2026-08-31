"""Preview what `mask: auto` makes of a video.

Temporary dev tool. Runs blur.mask.generate over one or more videos and writes out what it decided:
the mask itself, and the mask painted over a frame so you can see whether it caught the HUD and missed
everything else. `--stages` also dumps the scores the analysis measures, which is what the
stillness and detail settings cut through.

Call it through tools/automask-preview.sh - it needs the bundled python, which is the only one with
vapoursynth in it.
"""

import argparse
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "src" / "vapoursynth"))

import vapoursynth as vs  # noqa: E402
from vapoursynth import core  # noqa: E402

import blur.mask as mask  # noqa: E402
import blur.utils as u  # noqa: E402


def write_png(clip: vs.VideoNode, path: Path, ffmpeg: Path):
    """Write a one frame clip out as a png, by handing ffmpeg the raw planes."""
    frame = clip.get_frame(0)

    if clip.format.color_family == vs.RGB:
        # vapoursynth orders rgb planes r,g,b - ffmpeg's gbrp wants them g,b,r
        pix_fmt, planes = "gbrp", (1, 2, 0)
    else:
        pix_fmt, planes = "gray", (0,)

    # tobytes rather than bytes() - planes are padded out to a stride, and only tobytes drops the padding
    data = b"".join(frame[p].tobytes() for p in planes)

    subprocess.run(
        [
            str(ffmpeg), "-hide_banner", "-loglevel", "error", "-y",
            "-f", "rawvideo",
            "-pix_fmt", pix_fmt,
            "-s", f"{clip.width}x{clip.height}",
            "-i", "-",
            str(path),
        ],
        input=data,
        check=True,
    )  # fmt: skip


def to_rgbs(clip: vs.VideoNode) -> vs.VideoNode:
    """Float rgb, for compositing. Guesses a matrix the way blur.py does when the video doesn't say."""
    if clip.format.color_family == vs.RGB:
        return core.resize.Bicubic(clip, format=vs.RGBS)

    matrix = clip.get_frame(0).props.get("_Matrix", vs.MATRIX_UNSPECIFIED)
    if matrix in (0, vs.MATRIX_UNSPECIFIED):
        matrix = u.guess_matrix(clip.width, clip.height)

    return core.resize.Bicubic(clip, format=vs.RGBS, matrix_in=matrix)


def to_gray8(clip: vs.VideoNode) -> vs.VideoNode:
    return core.resize.Point(
        clip, format=vs.GRAY8, range_in_s="full", range_s="full", dither_type="none"
    )


def tint_protected(rgbs: vs.VideoNode, gray: vs.VideoNode, strength: float):
    """Paint the protected (black) parts of the mask red over the frame."""
    matched = mask.match(gray, rgbs)

    # y is the mask, so 1 - y is how protected the pixel is
    red = f"x 1 y - {strength} * + 0 max 1 min"
    other = f"x 1 1 y - {strength} * - * 0 max 1 min"

    return core.std.Expr([rgbs, matched], [red, other, other])


def fraction_static(clip: vs.VideoNode) -> float:
    return core.std.PlaneStats(clip).get_frame(0).props["PlaneStatsAverage"]


def detection_stages(clip: vs.VideoNode):
    """The scores mask.measure() works out, and what the thresholds make of them.

    The scores are the analysis's own intermediate rather than a copy of it, so this can't drift from what
    blur actually does. The greys in them are what the stillness and detail settings cut through.
    """
    params = mask.Params()

    scores = mask.measure(clip, params.samples)
    if scores is None:
        return None, None, None

    height = scores.height // 2
    static = core.std.Crop(scores, bottom=height)
    detail = core.std.Crop(scores, top=height)

    return static, detail, mask.shape(scores, params)


def preview(path: Path, args, ffmpeg: Path) -> bool:
    clip = core.bs.VideoSource(source=str(path), cachemode=0, showprogress=False)

    print(f"\n=== {path.name} ===")
    print(f"{clip.width}x{clip.height}, {clip.num_frames} frames, {clip.fps} fps")

    out = args.out / path.stem
    out.parent.mkdir(parents=True, exist_ok=True)

    written = []

    gray = None

    if args.stages:
        static, detail, gray = detection_stages(clip)

        if static is not None:
            for name, stage in (("score-static", static), ("score-detail", detail)):
                dest = out.with_suffix(f".{name}.png")
                write_png(stage, dest, ffmpeg)
                written.append(dest)
    else:
        gray = mask.generate(clip)

    if gray is None:
        print("-> no mask generated, this video would render unmasked")
    else:
        protected = 1 - fraction_static(gray)
        print(f"-> mask protects {protected:.3%} of the frame (grown and feathered)")

        frame = clip.num_frames // 2 if args.frame is None else args.frame
        frame = max(0, min(frame, clip.num_frames - 1))
        print(f"   previewing over frame {frame}")

        rgbs = to_rgbs(clip[frame])

        for name, image in (
            ("frame", core.resize.Point(rgbs, format=vs.RGB24, dither_type="none")),
            ("mask", to_gray8(gray)),
            (
                "overlay",
                core.resize.Point(
                    tint_protected(rgbs, gray, args.tint),
                    format=vs.RGB24,
                    dither_type="none",
                ),
            ),
        ):
            dest = out.with_suffix(f".{name}.png")
            write_png(image, dest, ffmpeg)
            written.append(dest)

    for dest in written:
        print(
            f"   wrote {dest.relative_to(REPO) if dest.is_relative_to(REPO) else dest}"
        )

    return gray is not None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("videos", nargs="+", type=Path)
    parser.add_argument(
        "--out",
        type=Path,
        default=REPO / "test_outputs" / "automask",
        help="where to write the pngs (default: test_outputs/automask, which is gitignored)",
    )
    parser.add_argument(
        "--frame",
        type=int,
        help="frame to paint the mask over (default: the middle of the video)",
    )
    parser.add_argument(
        "--stages",
        action="store_true",
        help="also write the scores behind the mask, for tuning stillness and detail",
    )
    parser.add_argument(
        "--tint",
        type=float,
        default=0.55,
        help="how strongly to paint protected areas red",
    )
    parser.add_argument(
        "--resources", type=Path, required=True, help="the built app's Resources folder"
    )
    parser.add_argument(
        "--no-open", action="store_true", help="don't open the output folder when done"
    )
    args = parser.parse_args()

    u.DEBUG_ENABLED = True  # so generate() says what it found

    ffmpeg = args.resources / "ffmpeg" / "ffmpeg"
    core.std.LoadPlugin(
        path=str(args.resources / "vapoursynth-plugins" / "bestsource.dylib")
    )

    args.out.mkdir(parents=True, exist_ok=True)

    generated = 0
    for path in args.videos:
        if not path.exists():
            print(f"\n=== {path} ===\nnot found")
            continue
        generated += preview(path, args, ffmpeg)

    print(f"\n{generated}/{len(args.videos)} videos got a mask. output in {args.out}")

    if not args.no_open and sys.platform == "darwin" and generated:
        subprocess.run(["open", str(args.out)], check=False)


if __name__ == "__main__":
    main()
