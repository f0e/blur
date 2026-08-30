"""Deduplication - putting back the frames a recording dropped.

A game rendering at 30fps captured at 60 gives you every frame twice. Blending across those pairs is what makes
blurred output look like it stutters: half of the blur window is the same picture held still. The fix is to
work out which frames are repeats and generate what should have been there instead.

This works by *retiming* rather than by patching frames in. A duplicate isn't replaced by an interpolated frame
at the rate the video already runs at - instead every frame the render asks for is worked out from the two
nearest frames that genuinely differ, at the time point it falls between them. Filling the gaps and
interpolating up to the output framerate become the same operation, done once:

    source   A . . B . . C            (`.` is a repeat of the frame before it)
    before   A a a B b b C            fill the gaps, then interpolate that again for the output framerate
    now      A - - - - - B - - - - - C    one pass, every frame drawn from a pair that was really captured

Doing it in one pass is the point. The old way interpolated interpolated frames: a gap was filled from the
frames around it, and then the interpolation pass proper generated its output from *those*, compounding
whatever the first pass got wrong and estimating motion from pictures no camera ever took. Here everything
that comes out is generated directly from two real frames, and the interpolator is asked for each frame once
instead of being spun up again for every run of duplicates.

Nothing is scanned up front. `analyse` builds a clip of per frame decisions - which two real frames bracket
this one - and each decision only looks a few frames either side of itself, so previewing one frame reads a
handful of frames rather than reading through the video.
"""

import vapoursynth as vs
from vapoursynth import core

from dataclasses import dataclass
from fractions import Fraction

from blur import log

# how far apart two frames are allowed to be and still have frames generated between them, and equally how
# far a frame looks for a pair. a longer gap means more movement to guess at from the same two pictures, and
# past a point the guess is worse than the stutter it replaces. this is what the 'deduplicate range' setting
# sets, and what its 'infinite' option lands on
MAX_GAP_LIMIT = 30

# what a decision frame carries. one per source frame: the two frames that bracket it, whether the picture
# is standing still (so there's nothing to interpolate towards), and the difference that was measured at it
PROP_LEFT = "BlurDedupeLeft"
PROP_RIGHT = "BlurDedupeRight"
PROP_HOLD = "BlurDedupeHold"
PROP_DIFF = "BlurDedupeDiff"


@dataclass(frozen=True)
class Dedupe:
    """Everything the interpolator needs to render a video as if it had never dropped a frame.

    `decisions` holds one frame per frame of the source, carrying the props above. It's a 1x1 clip - only the
    props matter - and it's built so that reading frame n only reads the source around n.
    """

    decisions: vs.VideoNode
    length: int
    max_gap: int


@dataclass(frozen=True)
class Bracket:
    """The two real frames an output frame sits between, and where between them it sits.

    `timepoint` is None when there's nothing to generate - the output frame lands exactly on `left`, or the
    picture isn't changing - and `left` should be used as it is.
    """

    left: int
    right: int
    timepoint: Fraction | None


def _shifted(clip: vs.VideoNode, offset: int) -> vs.VideoNode:
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


def _decisions(clip: vs.VideoNode, threshold: float, max_gap: int) -> vs.VideoNode:
    """Work out, for every frame of `clip`, which two real frames it sits between.

    A frame is *real* (rather than a repeat of the one before it) when it differs from its predecessor by at
    least `threshold`. Frame n's pair is then the nearest real frame at or before it and the nearest one after
    it, each looked for within `max_gap` frames:

      - nothing real within reach ahead, and the picture is standing still for as far as this frame can see.
        There's nothing to interpolate towards, so the frame is its own answer and gets held.
      - otherwise the pair is (`back`, `forward`), pulled in to at most `max_gap` apart. Pulling it in matters
        after a long still stretch: rather than crossfading the whole stretch into whatever comes next, the
        picture holds and then moves over the last `max_gap` frames before the change.

    Every frame in a gap works out the same pair, which is what makes this safe to decide a frame at a time -
    and a pair's own start frame works out that same pair too, which is what lets the interpolator be handed
    each pair once rather than once per frame that falls inside it. (`left` is either `back`, which is real and
    so answers with itself, or `forward - max_gap`, which is exactly `max_gap` from a `forward` that nothing
    real sits in front of - so it answers with itself either way.)
    """
    diffs = core.std.PlaneStats(clip, clip[0] + clip)

    # a decision at frame n reads max_gap either side of itself and no further
    offsets = list(range(-max_gap, max_gap + 1))
    window = [_shifted(diffs, offset) for offset in offsets]

    length = clip.num_frames
    last = length - 1

    # the props are the whole point of this clip, so its frames are as small as a frame gets
    holder = core.std.BlankClip(
        width=1, height=1, format=vs.GRAY8, length=length, keep=True
    )

    # window frames come after `holder` in the list handed to the selector
    base = 1 + offsets.index(0)

    def decide(n: int, f: list[vs.VideoFrame]) -> vs.VideoFrame:
        def diff(index: int) -> float:
            return f[base + index - n].props["PlaneStatsDiff"]  # type: ignore[return-value]

        def real(index: int) -> bool:
            # the first frame has nothing before it to repeat, so it always counts
            return index == 0 or diff(index) >= threshold

        back = None
        for index in range(n, max(n - max_gap, 0) - 1, -1):
            if real(index):
                back = index
                break

        forward = None
        for index in range(n + 1, min(n + max_gap, last) + 1):
            if real(index):
                forward = index
                break

        out = f[0].copy()

        if forward is None:
            out.props[PROP_LEFT] = n
            out.props[PROP_RIGHT] = n
            out.props[PROP_HOLD] = 1
        else:
            start = forward - max_gap
            out.props[PROP_LEFT] = start if back is None else max(back, start)
            out.props[PROP_RIGHT] = forward
            out.props[PROP_HOLD] = 0

        out.props[PROP_DIFF] = diff(n)

        return out

    return core.std.ModifyFrame(holder, [holder, *window], decide)


def analyse(clip: vs.VideoNode, threshold: float, max_gap: int | None) -> Dedupe:
    """Set up deduplication for `clip`, without reading any of it yet."""
    if max_gap is None:
        log.info(f"deduplication: unlimited range, capped at {MAX_GAP_LIMIT} frames")
        max_gap = MAX_GAP_LIMIT
    else:
        max_gap = max(1, min(int(max_gap), MAX_GAP_LIMIT))

    log.info(f"deduplicating (threshold {threshold}, up to {max_gap} frames apart)")

    return Dedupe(
        decisions=_decisions(clip, threshold, max_gap),
        length=clip.num_frames,
        max_gap=max_gap,
    )


def source_time(n: int, ratio: Fraction) -> Fraction:
    """Where output frame `n` falls on the source's timeline, measured in source frames.

    `ratio` is how many output frames there are to a source frame, so this is just the inverse - but it's the
    one conversion everything here turns on, and it's exact rather than floating point so that an output frame
    that lands squarely on a source frame is recognised as landing on it.
    """
    return Fraction(n) / ratio


def output_frames(length: int, ratio: Fraction) -> int:
    return max(1, int(length * ratio))


def over_output(dedupe: Dedupe, dst_frames: int, ratio: Fraction) -> vs.VideoNode:
    """`dedupe.decisions` re-indexed onto the output timeline, for use as a FrameEval prop_src."""
    decisions = dedupe.decisions
    last = dedupe.length - 1

    return core.std.FrameEval(
        core.std.BlankClip(decisions, length=dst_frames, keep=True),
        lambda n: decisions[min(int(source_time(n, ratio)), last)],
    )


def bracket(props, time: Fraction) -> Bracket:
    """Read a decision frame's props back out for an output frame at `time`."""
    left = int(props[PROP_LEFT])
    right = int(props[PROP_RIGHT])

    if props[PROP_HOLD] or right <= left or time <= left:
        return Bracket(left, right, None)

    return Bracket(left, right, Fraction(time - left, right - left))


def involved(at: Bracket) -> bool:
    """Whether deduplication had a hand in this frame, rather than it being plain interpolation.

    A pair one frame wide is two frames the recording really captured back to back, so anything generated
    between them is ordinary interpolation. Anything else is deduplication's doing: a wider pair spans
    frames that were dropped, and a pair of no width at all is the picture being held because nothing new
    turned up within range.
    """
    return at.right - at.left != 1


def describe(n: int, time: Fraction, props, at: Bracket) -> str:
    """A line about how one output frame was put together, for the debug overlay."""
    if at.timepoint is None:
        where = f"held on {at.left}" if at.left == at.right else f"on {at.left}"
    else:
        where = f"{at.left}->{at.right} @ {float(at.timepoint):.3f}"

    return f"{n} | src {float(time):.3f} | diff {float(props[PROP_DIFF]):.6f} | {where}"


def annotate(video: vs.VideoNode, dedupe: Dedupe, ratio: Fraction) -> vs.VideoNode:
    """Label the frames deduplication had a hand in, for the debug setting.

    Only those frames get written on, so what stands out against a plain render is exactly where the
    recording dropped something - an interpolated frame between two frames that were both really captured
    is left alone.

    This runs on the finished frames rather than inside the interpolation, both because that's the only
    place the text is sure to survive and because it's the only place the format is sure to take it - the
    tensorrt path interpolates in half float, which text can't be drawn on.
    """
    decisions = over_output(dedupe, video.num_frames, ratio)

    def label(n: int, f: vs.VideoFrame) -> vs.VideoNode:
        time = source_time(n, ratio)
        at = bracket(f.props, time)

        if not involved(at):
            return video

        return core.text.Text(video, describe(n, time, f.props, at), alignment=8)

    return core.std.FrameEval(video, label, prop_src=decisions)


def fill_drops_old(clip, threshold=0.1, debug=False):
    """The original deduplication, kept for the 'old' method.

    Every duplicate is replaced by a blend of its neighbours at the halfway point, whether or not halfway is
    where it belongs, and the result is then interpolated again by the pass after this one. It's cheap, and
    that's the whole of its case.
    """
    if not isinstance(clip, vs.VideoNode):
        raise ValueError("This is not a clip")

    differences = core.std.PlaneStats(clip, clip[0] + clip)

    super = core.mv.Super(clip)
    forward_vectors = core.mv.Analyse(super, isb=False)
    backwards_vectors = core.mv.Analyse(super, isb=True)
    filldrops = core.mv.FlowInter(
        clip, super, mvbw=backwards_vectors, mvfw=forward_vectors, ml=1
    )

    def selectFunc(n, f):
        if f.props["PlaneStatsDiff"] < threshold:
            if debug:
                return core.text.Text(
                    filldrops,
                    f"interpolated, diff: {f.props['PlaneStatsDiff']:.3f}",
                    alignment=8,
                )

            return filldrops
        else:
            return clip

    return core.std.FrameEval(clip, selectFunc, prop_src=differences)
