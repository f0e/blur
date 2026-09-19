"""Retiming - rendering a video onto the timeline its frames really belong on.

A recording's frames often aren't where the file says they are. Some are repeats of the frame before, held
because the game didn't draw anything new; the ones that are real can belong at moments that aren't evenly
spaced. Either way, interpolating as though every frame were new and evenly spaced bakes the unevenness in.

A *timeline* says what really happened: which frames of the source are real pictures, and when each one
belongs. Two things build one - see blur/deduplicate.py, which works it out from the pictures alone, and
blur/frame_timing.py, which reads it from a log the recorder wrote. This module is what they build, and how
interpolation renders it.

Rendering works by retiming rather than by patching frames in. A repeat isn't replaced by an interpolated
frame at the rate the video already runs at - instead every frame the render asks for is worked out from the
two nearest real frames, at the time point it falls between them. Filling the gaps and interpolating up to the
output framerate become the same operation, done once:

    source   A . . B . . C            (`.` is a repeat of the frame before it)
    before   A a a B b b C            fill the gaps, then interpolate that again for the output framerate
    now      A - - - - - B - - - - - C    one pass, every frame drawn from a pair that was really captured

Doing it in one pass is the point. The old way interpolated interpolated frames: a gap was filled from the
frames around it, and then the interpolation pass proper generated its output from *those*, compounding
whatever the first pass got wrong and estimating motion from pictures no camera ever took. Here everything
that comes out is generated directly from two real frames, and the interpolator is asked for each frame once
instead of being spun up again for every run of repeats.

A timeline is carried as a clip of per frame *decisions* - which real frames bracket this frame, and when
they belong - rather than as one big list, so that nothing has to be scanned up front. A source that works
the timeline out from the pictures can build each decision from a few frames either side of itself, which is
what lets the GUI preview a single frame without reading through the video.
"""

import vapoursynth as vs
from vapoursynth import core

from dataclasses import dataclass
from fractions import Fraction

# how far apart two real frames are allowed to be and still have frames generated between them, and equally
# how far a frame looks for a pair. a longer gap means more movement to guess at from the same two pictures,
# and past a point the guess is worse than the stutter it replaces
MAX_GAP_LIMIT = 30

# what a decision frame carries: the real frames whose stretches of the timeline overlap it, in order, and when
# each of them belongs, counted in 1/PROP_TIME_SCALE frames. a single frame means the picture is held. the
# difference is only there when the timeline was measured from the pictures
PROP_FRAMES = "BlurRetimeFrames"
PROP_TIMES = "BlurRetimeTimes"
PROP_TIME_SCALE = "BlurRetimeTimeScale"
PROP_DIFF = "BlurDedupeDiff"


@dataclass(frozen=True)
class Timeline:
    """Everything the interpolator needs to render a video as if it had never dropped or delayed a frame.

    `decisions` holds `resolution` frames per frame of the source, carrying the props above. It's a 1x1 clip -
    only the props matter - and it's built so that reading one of its frames only reads the source around it.

    `slots` is how many pairs a decision can hold. Deduplication's decisions are always one pair, but a
    timeline whose frames don't land on whole frame numbers can have a real frame land partway through a
    decision.

    `max_gap` is how far apart the furthest pair in this timeline is, which is all interpolation needs it for
    - how far a picture is *allowed* to be carried before it's held instead is the source's business, and it
    has already been applied by the time a timeline exists.
    """

    decisions: vs.VideoNode
    length: int
    max_gap: int
    resolution: int
    slots: int = 1


@dataclass(frozen=True)
class Bracket:
    """The two real frames an output frame sits between, when they belong, and where between them it sits.

    `timepoint` is None when there's nothing to generate - the output frame lands on `left` or before it, or
    the picture isn't changing - and `left` should be used as it is. `slot` is which of the decision's pairs
    this is.
    """

    left: int
    right: int
    left_time: Fraction
    right_time: Fraction
    timepoint: Fraction | None
    slot: int = 0


def shifted(clip: vs.VideoNode, offset: int) -> vs.VideoNode:
    """`clip` moved along by `offset`, so its frame n holds whatever frame n + offset held.

    The ends repeat rather than running out, which is what makes a scan that reaches past the start or end of
    the video read as "nothing changes past here" instead of failing.
    """
    length = clip.num_frames
    offset = max(-(length - 1), min(offset, length - 1))

    if offset > 0:
        return clip[offset:] + clip[length - 1] * offset

    if offset < 0:
        return clip[0] * -offset + clip[: length + offset]

    return clip


def source_time(n: int, ratio: Fraction) -> Fraction:
    """Where output frame `n` falls on the source's timeline, measured in source frames.

    `ratio` is how many output frames there are to a source frame, so this is just the inverse - but it's the
    one conversion everything here turns on, and it's exact rather than floating point so that an output frame
    that lands squarely on a source frame is recognised as landing on it.
    """
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
    decisions = timeline.decisions

    return core.std.FrameEval(
        core.std.BlankClip(decisions, length=dst_frames, keep=True),
        lambda n: decisions[decision_index(timeline, n, ratio)],
    )


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

    if right_time <= left_time or time <= left_time:
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
    """Whether retiming had a hand in this frame, rather than it being plain interpolation.

    A pair one frame wide is two frames the recording really captured back to back, so anything generated
    between them is ordinary interpolation. Anything else is retiming's doing: a wider pair spans frames that
    were dropped, and a pair of no width at all is the picture being held because nothing new turned up
    within range.
    """
    return at.right_time - at.left_time != 1


def describe(n: int, time: Fraction, props, at: Bracket) -> str:
    """A line about how one output frame was put together, for the debug overlay."""
    if at.timepoint is None:
        where = f"held on {at.left}" if at.left == at.right else f"on {at.left}"
    else:
        where = (
            f"{at.left}->{at.right} ({float(at.left_time):g}->{float(at.right_time):g})"
            f" @ {float(at.timepoint):.3f}"
        )

    diff = f" | diff {float(props[PROP_DIFF]):.6f}" if PROP_DIFF in props else ""
    return f"{n} | src {float(time):.3f}{diff} | {where}"


def annotate(video: vs.VideoNode, timeline: Timeline, ratio: Fraction) -> vs.VideoNode:
    """Label the frames retiming had a hand in, for the debug setting.

    Only those frames get written on, so what stands out against a plain render is exactly where the
    recording dropped or delayed something - an interpolated frame between two frames that were both really
    captured back to back is left alone.

    This runs on the finished frames rather than inside the interpolation, both because that's the only
    place the text is sure to survive and because it's the only place the format is sure to take it - the
    tensorrt path interpolates in half float, which text can't be drawn on.
    """
    decisions = over_output(timeline, video.num_frames, ratio)

    def label(n: int, f: vs.VideoFrame) -> vs.VideoNode:
        time = source_time(n, ratio)
        at = bracket(f.props, time)

        if not involved(at):
            return video

        return core.text.Text(video, describe(n, time, f.props, at), alignment=8)

    return core.std.FrameEval(video, label, prop_src=decisions)
