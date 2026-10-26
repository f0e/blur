"""Retiming - rendering a video onto the timeline its frames really belong on.

A timeline says which source frames are real pictures and when each one belongs. It's built by
blur/deduplicate.py (from the pictures) or blur/frame_timing.py (from a log). Every output frame is interpolated
directly from the two nearest real frames, so filling gaps and interpolating to the output framerate happen in
one pass instead of interpolating already interpolated frames:

    source   A . . B . . C            (`.` is a repeat of the frame before it)
    output   A - - - - - B - - - - - C

The timeline is a clip of per frame decisions rather than a list, so nothing has to be scanned up front.
"""

import math
from dataclasses import dataclass
from fractions import Fraction

import numpy as np

import vapoursynth as vs
from vapoursynth import core

# the widest gap between real frames that still gets interpolated
MAX_GAP_LIMIT = 30

# a decision's real frames and their times (in 1/PROP_TIME_SCALE frames). a single frame means it's held
PROP_FRAMES = "BlurRetimeFrames"
PROP_TIMES = "BlurRetimeTimes"
PROP_TIME_SCALE = "BlurRetimeTimeScale"
# on the first decision of each source frame, the real frame at or after it. timelines whose pairs are always
# neighbouring real frames set it, which lets every pair be interpolated from one clip
PROP_NEXT = "BlurRetimeNext"
PROP_DIFF = "BlurDedupeDiff"


@dataclass(frozen=True)
class Timeline:
    """`decisions` is a 1x1 clip with `resolution` frames per source frame, carrying the props above. `slots` is
    how many pairs a decision can hold. `max_gap` is the widest pair in the timeline. When the timing knows them,
    `spans` is how many frames of time each source frame's change from the picture before covers (0 for repeats),
    and `still` which repeats are the picture really staying the same rather than pictures the recording missed.
    `real` is the real frames, their times and the scale of those, when they're known up front."""

    decisions: vs.VideoNode
    length: int
    max_gap: int
    resolution: int
    slots: int = 1
    spans: np.ndarray | None = None
    still: np.ndarray | None = None
    real: tuple[list[int], list[int], int] | None = None


@dataclass(frozen=True)
class Bracket:
    """The two real frames an output frame sits between. `timepoint` is None when `left` should be used as is."""

    left: int
    right: int
    left_time: Fraction
    right_time: Fraction
    timepoint: Fraction | None
    slot: int = 0


def shifted(clip: vs.VideoNode, offset: int) -> vs.VideoNode:
    """`clip` moved along by `offset`, repeating the ends rather than running out."""
    length = clip.num_frames
    offset = max(-(length - 1), min(offset, length - 1))

    if offset > 0:
        return clip[offset:] + clip[length - 1] * offset

    if offset < 0:
        return clip[0] * -offset + clip[: length + offset]

    return clip


def source_time(n: int, ratio: Fraction) -> Fraction:
    """Where output frame `n` falls on the source's timeline. Exact so landing on a source frame is detected."""
    return Fraction(n) / ratio


def output_frames(length: int, ratio: Fraction) -> int:
    return max(1, int(length * ratio))


def decision_index(timeline: Timeline, n: int, ratio: Fraction) -> int:
    """Which of `timeline.decisions`' frames covers output frame `n`."""
    return min(
        int(source_time(n, ratio) * timeline.resolution),
        timeline.decisions.num_frames - 1,
    )


def over_output(timeline: Timeline, dst_frames: int, ratio: Fraction) -> vs.VideoNode:
    """`timeline.decisions` re-indexed onto the output timeline, for use as a FrameEval prop_src."""
    decision = single_frames(timeline.decisions)

    return core.std.FrameEval(
        core.std.BlankClip(timeline.decisions, length=dst_frames, keep=True),
        lambda n: decision(decision_index(timeline, n, ratio)),
    )


def single_frames(clip: vs.VideoNode):
    """A lookup from a frame number to `clip` cut down to that frame, for FrameEval. Each cut is made once, since
    making a new one for every output frame adds up at high framerates."""
    cuts: dict[int, vs.VideoNode] = {}

    def frame(n: int) -> vs.VideoNode:
        n = min(n, clip.num_frames - 1)
        if n not in cuts:
            cuts[n] = clip[n]

        return cuts[n]

    return frame


def _ints(value) -> list[int]:
    # vapoursynth hands a one element array back as a plain value
    return [int(v) for v in value] if isinstance(value, (list, tuple)) else [int(value)]


def frame_at(props, index: int) -> int:
    """The `index`th real frame a decision holds, or its last if it holds fewer."""
    frames = _ints(props[PROP_FRAMES])
    return frames[min(index, len(frames) - 1)]


def bracket(props, time: Fraction) -> Bracket:
    """Read a decision frame's props back out for an output frame at `time`."""
    frames = _ints(props[PROP_FRAMES])
    scale = int(props[PROP_TIME_SCALE])
    times = [Fraction(t, scale) for t in _ints(props[PROP_TIMES])]

    if len(frames) == 1:
        return Bracket(frames[0], frames[0], times[0], times[0], None)

    slot = 0
    while slot + 2 < len(frames) and time >= times[slot + 1]:
        slot += 1

    left, right = frames[slot], frames[slot + 1]
    left_time, right_time = times[slot], times[slot + 1]

    if left == right or right_time <= left_time or time <= left_time:
        return Bracket(left, right, left_time, right_time, None, slot)

    return Bracket(
        left,
        right,
        left_time,
        right_time,
        min((time - left_time) / (right_time - left_time), Fraction(1)),
        slot,
    )


def involved(at: Bracket) -> bool:
    """Whether retiming had a hand in this frame, i.e. the pair isn't exactly one frame apart."""
    return at.right_time - at.left_time != 1


def describe(n: int, time: Fraction, props, at: Bracket) -> str:
    """A line about how one output frame was put together, for the debug overlay."""
    if at.timepoint is None:
        where = f"held on {at.left}" if at.left == at.right else f"on {at.left}"
    else:
        where = f"{at.left}->{at.right} ({float(at.left_time):g}->{float(at.right_time):g}) @ {float(at.timepoint):.3f}"

    diff = f" | diff {float(props[PROP_DIFF]):.6f}" if PROP_DIFF in props else ""
    return f"{n} | src {float(time):.3f}{diff} | {where}"


def nearest(clips: list[vs.VideoNode], timeline: Timeline, target: vs.VideoNode, ratio: Fraction) -> list[vs.VideoNode]:
    """`clips`' real frames stretched over `target`'s frames (and framerate), each output frame taking whichever
    real frame it's closest to in time. Which one that is gets worked out once for all of them."""
    decisions = over_output(timeline, target.num_frames, ratio)
    chosen: dict[int, int] = {}

    def real_frame(n: int, props) -> int:
        if n not in chosen:
            at = bracket(props, source_time(n, ratio))
            chosen[n] = at.right if at.timepoint is not None and at.timepoint >= Fraction(1, 2) else at.left

        return chosen[n]

    def stretched(clip: vs.VideoNode) -> vs.VideoNode:
        frame = single_frames(clip)
        holder = core.std.BlankClip(clip, length=target.num_frames, fpsnum=target.fps_num, fpsden=target.fps_den)
        return core.std.FrameEval(holder, lambda n, f: frame(real_frame(n, f.props)), prop_src=decisions)

    return [stretched(clip) for clip in clips]


def listed(frames: list[int], times: list[int], scale: int, length: int, slots: int | None = None, **extra) -> Timeline:
    """A timeline from its real frames and their times in 1/`scale` frames, both in order. A frame listed twice in a
    row is held between the two. Without `slots`, decisions hold as many pairs as they need."""
    end = max(length, frames[-1] + 1)
    starts = np.arange(end, dtype=np.int64) * scale
    begins = np.maximum(np.searchsorted(times, starts, "right") - 1, 0)
    stops = np.minimum(np.searchsorted(times, starts + scale, "left"), len(times) - 1)
    stops = np.maximum(stops, begins)
    if slots is None:
        slots = max(1, int(np.max(stops - begins)))
    stops = np.minimum(stops, begins + slots)
    following = np.minimum(np.searchsorted(frames, np.arange(end), "left"), len(frames) - 1)

    # a held picture isn't interpolated, so only gaps between different frames count
    gaps = [(b - a) / scale for a, b, left, right in zip(times, times[1:], frames, frames[1:]) if left != right]
    widest = int(max(2, math.ceil(max(gaps, default=1.0))))

    holder = core.std.BlankClip(width=1, height=1, format=vs.GRAY8, length=end, keep=True)

    def decide(n: int, f: vs.VideoFrame) -> vs.VideoFrame:
        begin, stop = int(begins[n]), int(stops[n])
        out = f.copy()
        out.props[PROP_FRAMES] = frames[begin : stop + 1]
        out.props[PROP_TIMES] = times[begin : stop + 1]
        out.props[PROP_TIME_SCALE] = scale
        out.props[PROP_NEXT] = frames[following[n]]
        return out

    return Timeline(
        decisions=core.std.ModifyFrame(holder, holder, decide),
        length=length,
        max_gap=widest,
        resolution=1,
        slots=slots,
        real=(frames, times, scale),
        **extra,
    )


def spaced(timeline: Timeline, ratio: Fraction) -> tuple[Timeline, Timeline]:
    """For interpolating to `ratio` times the framerate in two passes, where only the second places frames. The
    first makes each real frame and evenly spaced steps between neighbouring ones, about `ratio` per frame of time
    between them, which an interpolator taking a time point does in one run each rather than one per off-grid
    frame. Returns the first pass's timeline, which lands each of its frames on one of those, and the second's,
    which places them on the first's output."""
    assert timeline.real is not None
    frames, times, scale = timeline.real

    first_frames: list[int] = []
    first_times: list[int] = []
    second_frames: list[int] = []
    second_times: list[int] = []
    made = 0

    def place(index: int, time: Fraction):
        at = round(time * ratio)
        # crowded frames are kept apart by the smallest step, like the timeline they come from
        if second_times and at <= second_times[-1]:
            at = second_times[-1] + 1
        second_frames.append(index)
        second_times.append(at)

    for i, (frame, time) in enumerate(zip(frames, times)):
        if i > 0 and frames[i - 1] == frame:
            place(second_frames[-1], Fraction(time))
            continue

        # the first pass's output frame n is at n / ratio
        first_frames.append(frame)
        first_times.append(made * ratio.denominator)
        place(made, Fraction(time))
        made += 1

        if i + 1 < len(frames) and frames[i + 1] != frame:
            gap = Fraction(times[i + 1] - time)
            steps = max(1, round(gap / scale * ratio))
            for step in range(1, steps):
                place(made, time + gap * Fraction(step, steps))
                made += 1

    first = listed(first_frames, first_times, ratio.numerator, max(timeline.length, math.ceil(made / ratio)))
    second = listed(second_frames, second_times, scale, output_frames(timeline.length, ratio))
    return first, second


def annotate(video: vs.VideoNode, timeline: Timeline, ratio: Fraction) -> vs.VideoNode:
    """Label the frames retiming had a hand in, for the debug setting. Runs on finished frames since tensorrt
    interpolates in half float, which text can't be drawn on."""
    decisions = over_output(timeline, video.num_frames, ratio)

    def label(n: int, f: vs.VideoFrame) -> vs.VideoNode:
        time = source_time(n, ratio)
        at = bracket(f.props, time)

        if not involved(at):
            return video

        return core.text.Text(video, describe(n, time, f.props, at), alignment=8)

    return core.std.FrameEval(video, label, prop_src=decisions)
