"""Scene detection - finding cuts, so nothing is interpolated or blurred across them.

A cut changes the picture a lot compared to the changes around it, where fast motion changes it a lot for several
frames in a row. Tuned on hand labelled gameplay, montages and film at 24-1200fps. pyscenedetect, ffmpeg's scdet,
mvtools and misc.SCDetect were tried too, and mistook fast motion for cuts far more often.
"""

import itertools
import math
import statistics
from fractions import Fraction

import numpy as np

import vapoursynth as vs
from blur import retime
from vapoursynth import core

PROP_CUT = "BlurSceneCut"  # the frame starts a new scene
PROP_NEXT = "BlurSceneNext"  # the frame after does
PROP_HELD = "BlurSceneHeld"
PROP_LEFT = "BlurSceneLeft"  # the real frame a held frame shows
PROP_WITHIN = "BlurSceneWithin"  # blended within its scene

# a retiming decision's pairs with a cut between them, flattened as left frame, left time, right time
PROP_CROSSING = "BlurSceneCrossing"

# for the debug overlay
PROP_CHANGE = "BlurSceneChange"
PROP_MOTION = "BlurSceneMotion"
PROP_BLOCKS = "BlurSceneBlocks"

WIDTH = 160
BLOCK = 10

# how much a block has to change to count towards the changed fraction
BLOCK_CHANGE = 0.08

# changes smaller than this are repeated frames, which say nothing about how fast the picture is moving
REPEAT = 0.002

# how many changes either side a frame is compared with
NEIGHBOURS = 3

# how far past repeated frames to look for those changes. duplicated footage changes at a steady rhythm, and looking
# twice that far finds real motion. anything else only looks a little way, since a still stretch between two cuts
# would otherwise make each look like the motion around the other
REACH = 24
IRREGULAR_REACH = 8
MIN_REACH = 3
STEADY_GAPS = 4
STEADY_SPREAD = 1.5

# stops still footage making tiny changes look huge
MOTION_FLOOR = 0.005

# a big change well above the motion around it, or a smaller one far above it over a good part of the frame. the
# smaller one is for cuts between dark or mostly black frames, e.g. scoped in
BIG_CHANGE = 0.12
BIG_RATIO = 2.5
SMALL_CHANGE = 0.04
SMALL_RATIO = 8
SMALL_BLOCKS = 0.25

# vapoursynth's automatic caching drops frames too soon for the windows read here. detection reads the source well
# ahead of everything else, and seeking back to frames it's dropped doubles render times on long gop video
CACHED = 256
SOURCE_CACHE_BYTES = 1 << 30
SOURCE_CACHE_FRAMES = (2 * REACH, 4 * REACH)


def _cached(clip: vs.VideoNode, frames: int = CACHED) -> vs.VideoNode:
    if hasattr(core.std, "SetVideoCache"):
        core.std.SetVideoCache(clip, mode=1, fixedsize=1, maxsize=frames)

    return clip


def _frame_bytes(clip: vs.VideoNode) -> int:
    fmt = clip.format
    chroma = 0 if fmt.num_planes == 1 else 2 / (1 << (fmt.subsampling_w + fmt.subsampling_h))
    return int(clip.width * clip.height * fmt.bytes_per_sample * (1 + chroma))


def _metrics(video: vs.VideoNode, full_range: bool) -> vs.VideoNode:
    luma = core.std.ShufflePlanes(video, planes=0, colorfamily=vs.GRAY)
    height = max(BLOCK, round(WIDTH * video.height / video.width))

    small = core.resize.Bilinear(luma, width=WIDTH, height=height, format=vs.GRAYS, range_in=full_range, range=True)
    before = retime.shifted(small, -1)

    rows, cols = height // BLOCK, WIDTH // BLOCK
    holder = core.std.BlankClip(
        width=1,
        height=1,
        format=vs.GRAY8,
        length=video.num_frames,
        fpsnum=video.fps_num,
        fpsden=video.fps_den,
        keep=True,
    )

    def measure(n: int, f: list[vs.VideoFrame]) -> vs.VideoFrame:
        diff = np.abs(np.asarray(f[1][0]) - np.asarray(f[2][0]))
        blocks = diff[: rows * BLOCK, : cols * BLOCK].reshape(rows, BLOCK, cols, BLOCK).mean((1, 3))

        return _with_props(
            f[0], **{PROP_CHANGE: float(diff.mean()), PROP_BLOCKS: float((blocks > BLOCK_CHANGE).mean())}
        )

    return _cached(core.std.ModifyFrame(holder, [holder, small, before], measure))


def _reach(gaps: list[int]) -> int:
    if not gaps:
        return IRREGULAR_REACH

    steady = len(gaps) >= STEADY_GAPS and max(gaps) <= STEADY_SPREAD * min(gaps)
    return int(min(REACH if steady else IRREGULAR_REACH, max(MIN_REACH, 2 * statistics.median(gaps))))


def detect(video: vs.VideoNode, full_range: bool) -> vs.VideoNode:
    """Which frames of `video` start a new scene. Makes `video` keep more frames cached, see SOURCE_CACHE_BYTES."""
    low, high = SOURCE_CACHE_FRAMES
    _cached(video, max(low, min(SOURCE_CACHE_BYTES // _frame_bytes(video), high)))

    metrics = _metrics(video, full_range)
    window = [retime.shifted(metrics, offset) for offset in range(-REACH, REACH + 1)]
    last = video.num_frames - 1

    def decide(n: int, f: list[vs.VideoFrame]) -> vs.VideoFrame:
        def change(index: int) -> float:
            return f[1 + index - n + REACH].props[PROP_CHANGE]  # type: ignore[return-value]

        cut = False
        here = around = blocks = 0.0
        if n > 0:
            nearby = range(max(1, n - REACH), min(last, n + REACH) + 1)
            moved = [index for index in nearby if change(index) > REPEAT]
            reach = _reach([b - a for a, b in itertools.pairwise(moved)])

            def motion(step: int) -> float:
                found = []
                index = n + step
                while 0 < index <= last and abs(index - n) <= reach and len(found) < NEIGHBOURS:
                    if change(index) > REPEAT:
                        found.append(change(index))
                    index += step

                return max(found, default=0.0)

            here = change(n)
            around = max((motion(-1) + motion(1)) / 2, MOTION_FLOOR)
            blocks = f[1 + REACH].props[PROP_BLOCKS]

            cut = (here > BIG_CHANGE and here > BIG_RATIO * around) or (
                here > SMALL_CHANGE and here > SMALL_RATIO * around and blocks > SMALL_BLOCKS
            )

        return _with_props(f[0], **{PROP_CUT: int(cut), PROP_CHANGE: here, PROP_MOTION: around, PROP_BLOCKS: blocks})

    holder = core.std.BlankClip(metrics, keep=True)
    return _cached(core.std.ModifyFrame(holder, [holder, *window], decide))


def _on_output(clip: vs.VideoNode, length: int, ratio: Fraction) -> vs.VideoNode:
    """`clip` stretched onto a timeline `ratio` times as fast."""
    if ratio.denominator == 1:
        repeated = core.std.Interleave([clip] * ratio.numerator)
        return (repeated + repeated[-1] * length)[:length]

    last = clip.num_frames - 1
    return core.std.FrameEval(
        core.std.BlankClip(clip, length=length, keep=True),
        lambda n: clip[min(n * ratio.denominator // ratio.numerator, last)],
    )


def _hold_steady(interpolated: vs.VideoNode, cuts: vs.VideoNode, ratio: Fraction) -> vs.VideoNode:
    length = interpolated.num_frames
    last = cuts.num_frames - 1
    num, den = ratio.numerator, ratio.denominator

    pairs = _cached(
        core.std.ModifyFrame(
            cuts,
            [cuts, retime.shifted(cuts, 1)],
            lambda n, f: _with_props(f[0], **{PROP_NEXT: int(n < last and f[1].props[PROP_CUT])}),
        )
    )

    def decide(n: int, f: vs.VideoFrame) -> vs.VideoFrame:
        # output frame n is at source time n * den / num
        left = n * den // num
        between = n * den % num != 0

        starts = f.props[PROP_CUT] and n > 0 and (n - 1) * den // num < left

        return _with_props(f, **{PROP_HELD: int(between and f.props[PROP_NEXT]), PROP_CUT: int(starts)})

    mapped = _on_output(pairs, length, ratio)
    return _cached(core.std.ModifyFrame(mapped, mapped, decide))


def _hold_retimed(
    interpolated: vs.VideoNode, cuts: vs.VideoNode, ratio: Fraction, timeline: retime.Timeline
) -> vs.VideoNode:
    length = interpolated.num_frames
    last = cuts.num_frames - 1
    num, den = ratio.numerator, ratio.denominator

    # how far from its own frame a decision's frames can be
    reach = 2 * timeline.max_gap + 2
    window = [retime.shifted(cuts, offset) for offset in range(-reach, reach + 1)]

    def crossings(n: int, f: list[vs.VideoFrame]) -> vs.VideoFrame:
        # the frames between a pair are repeats of one side, so the cut can be at any of them
        source = n // timeline.resolution

        def cut(index: int) -> bool:
            offset = index - source
            return 0 < index <= last and -reach <= offset <= reach and bool(f[1 + offset + reach].props[PROP_CUT])

        frames = _ints(f[0].props[retime.PROP_FRAMES])
        times = _ints(f[0].props[retime.PROP_TIMES])
        pairs = [
            (frames[k], times[k], times[k + 1])
            for k in range(len(frames) - 1)
            if any(cut(index) for index in range(frames[k] + 1, frames[k + 1] + 1))
        ]

        return _with_props(f[0], **{PROP_CROSSING: [value for pair in pairs for value in pair]}) if pairs else f[0]

    decisions = _cached(core.std.ModifyFrame(timeline.decisions, [timeline.decisions, *window], crossings))
    on_output = _on_output(decisions, length, ratio / timeline.resolution)

    def crossing(props) -> list[tuple[int, int, int]]:
        values = _ints(props.get(PROP_CROSSING, []))
        return [tuple(values[k : k + 3]) for k in range(0, len(values), 3)]  # type: ignore[misc]

    def decide(n: int, f: list[vs.VideoFrame]) -> vs.VideoFrame:
        # times compared as integers, output frame n is at source time n * den / num
        scale = int(f[0].props[retime.PROP_TIME_SCALE])
        now, before = n * scale * den, (n - 1) * scale * den

        held = None
        for left, left_time, right_time in crossing(f[0].props):
            if left_time * num < now < right_time * num:
                held = left

        # the cut is where the right of a crossing pair takes over, which can be in the frame before's decision
        starts = n > 0 and any(
            before < right_time * num <= now
            for props in (f[0].props, f[1].props)
            for _, _, right_time in crossing(props)
        )

        left = {} if held is None else {PROP_LEFT: held}
        return _with_props(f[0], **{PROP_HELD: int(held is not None), PROP_CUT: int(starts), **left})

    return _cached(core.std.ModifyFrame(on_output, [on_output, retime.shifted(on_output, -1)], decide))


def _ints(value) -> list[int]:
    # vapoursynth hands a one element array back as a plain value
    return [int(v) for v in value] if isinstance(value, (list, tuple)) else [int(value)]


def _with_props(frame: vs.VideoFrame, **props) -> vs.VideoFrame:
    out = frame.copy()
    for key, value in props.items():
        out.props[key] = value
    return out


def hold(
    interpolated: vs.VideoNode,
    source: vs.VideoNode,
    cuts: vs.VideoNode,
    timeline: retime.Timeline | None = None,
) -> tuple[vs.VideoNode, vs.VideoNode]:
    """`interpolated` showing the real frame before each cut until the cut, rather than interpolating across it, and
    the cuts on its timeline."""
    ratio = Fraction(interpolated.fps) / Fraction(source.fps)
    length = interpolated.num_frames

    # svp hands back 8 bit whatever it was given
    if source.format.id != interpolated.format.id:
        source = core.resize.Point(source, format=interpolated.format.id)

    if timeline is None:
        scenes = _hold_steady(interpolated, cuts, ratio)
        before_cut = _on_output(source, length, ratio)
    else:
        scenes = _hold_retimed(interpolated, cuts, ratio, timeline)
        before_cut = core.std.FrameEval(interpolated, lambda n, f: source[f.props[PROP_LEFT]], prop_src=scenes)

    # only reads `before_cut` for held frames
    held = core.akarin.Select([interpolated, before_cut], scenes, f"x.{PROP_HELD}")

    return held, scenes


def within(clip: vs.VideoNode, radius: int, cuts: vs.VideoNode, process) -> vs.VideoNode:
    """`process(clip)` for a filter reading `radius` frames either side, run on each scene separately near cuts.
    `process` has to repeat a clip's end frames past its ends, like FrameBlend."""
    processed = process(clip)
    if radius <= 0:
        return processed

    length = clip.num_frames
    offsets = range(-radius + 1, radius + 1)
    window = [retime.shifted(cuts, offset) for offset in offsets]

    def pick(n: int, f: list[vs.VideoFrame]) -> vs.VideoNode:
        starts = [
            offset
            for offset, frame in zip(offsets, f, strict=True)
            if 0 < n + offset < length and frame.props[PROP_CUT]
        ]

        if not starts:
            return processed

        begin = n + max((offset for offset in starts if offset <= 0), default=-n)
        end = n + min((offset for offset in starts if offset > 0), default=length - n)

        return core.std.SetFrameProps(process(clip[begin:end])[n - begin], **{PROP_WITHIN: 1})

    return core.std.FrameEval(processed, pick, prop_src=window)


# changes worth showing in the debug overlay even though they weren't cuts
NEAR_RATIO = 1.5
NEAR_CHANGE = 0.02


def annotate(video: vs.VideoNode, scenes: vs.VideoNode, detected: vs.VideoNode) -> vs.VideoNode:
    """`scenes` is on `video`'s timeline, `detected` (from `detect`) on the source's."""
    ratio = Fraction(video.fps) / Fraction(detected.fps)
    last = detected.num_frames - 1
    source = core.std.FrameEval(
        core.std.BlankClip(detected, length=video.num_frames, keep=True),
        # frames between two source frames are showing the change into the second
        lambda n: detected[min(math.ceil(n / ratio), last)],
    )

    def label(n: int, f: list[vs.VideoFrame]) -> vs.VideoNode:
        here, detection = f[0].props, f[1].props
        change, motion = float(detection[PROP_CHANGE]), float(detection[PROP_MOTION])
        times = change / max(motion, MOTION_FLOOR)

        lines = []
        if here.get(PROP_HELD):
            lines.append("held, scene cut next")
        elif here.get(PROP_CUT):
            lines.append("scene cut")

        if detection[PROP_CUT] or (times >= NEAR_RATIO and change >= NEAR_CHANGE):
            verdict = "cut" if detection[PROP_CUT] else "not a cut"
            lines.append(
                f"change {change:.3f} | {times:.1f}x motion {motion:.3f} | {float(detection[PROP_BLOCKS]):.0%} of blocks"
                f" | {verdict}"
            )

        if not lines:
            return video

        return core.text.Text(video, "\n".join(lines), alignment=2)

    return core.std.FrameEval(video, label, prop_src=[scenes, source])


def annotate_blur(video: vs.VideoNode) -> vs.VideoNode:
    return core.std.FrameEval(
        video,
        lambda n, f: (
            core.text.Text(video, "blur kept within scene", alignment=1) if f.props.get(PROP_WITHIN) else video
        ),
        prop_src=video,
    )
