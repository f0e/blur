"""Deduplication - working out a timeline from the pictures alone.

A game rendering at 30fps captured at 60 gives you every frame twice. Blending across those pairs is what makes
blurred output look like it stutters: half of the blur window is the same picture held still. The fix is to
work out which frames are repeats and generate what should have been there instead.

Two questions have to be answered, and with nothing but the video to go on only the first has a certain
answer:

- **which frames are repeats.** A frame that differs from the one before it by less than `threshold` is one,
  which divides the video into runs of identical frames.
- **when each run's real picture belongs.** Nothing in the file says, and `timing` below is the choice of
  which guess to make.

The answers become a timeline, which interpolation renders - see blur/retime.py for what that is and how.
Where the recording came with a frame timing log, blur/frame_timing.py builds the same timeline from measured
times instead of guessing, and this doesn't run at all.

Nothing is scanned up front. `analyse` builds a clip of per frame decisions, and each decision only looks a
few frames either side of itself, so previewing one frame reads a handful of frames rather than reading
through the video.
"""

import blur.utils as u
import vapoursynth as vs
from blur import log, retime
from vapoursynth import core

# Which frame of a run of repeats is the real one.
#
# When a recording drops a frame it repeats one to fill the slot, and nothing in the file says which of the
# repeats is the picture that was really drawn. The picture can't tell you either, and it isn't decidable in
# principle: anchoring every run at its first frame and anchoring every run at its last frame produce the same
# sequence of intervals, one shifted along by a run, so no measurement of the frames can separate them.
#
# What it changes is which interval each picture is given. FIRST hands a picture the length of the run it
# starts; LAST hands it the length of the run before it. So it only matters where run lengths vary, and there
# it's the difference between motion at a steady rate and motion that speeds up and slows down.
#
#   FIRST        the picture arrived at the start of the run and was repeated after it. this is what a live
#                recording does, and all it can do - nothing shows a frame before it was drawn.
#   LAST         the run ends on the real frame. a variable rate recording resampled to a fixed one can land
#                its frames a slot later, and that reads this way.
#   CENTER       split the difference and put the picture in the middle of its run. can't be more than half a
#                run out whichever way the footage leans, where guessing an end can be a whole run out.
#   SURROUNDING  don't believe the run at all - work from the frames either side of it. that's exact under
#                both of the above rather than a compromise between them, because the frames outside a run
#                aren't the ones in question. it pays for that by generating across a longer gap, which is
#                more for the interpolator to get wrong, and by needing `deduplicate range` wide enough to
#                fit the run plus a frame each side.
TIMING_FIRST = "first"
TIMING_LAST = "last"
TIMING_CENTER = "center"
TIMING_SURROUNDING = "surrounding"
TIMINGS = [TIMING_FIRST, TIMING_LAST, TIMING_CENTER, TIMING_SURROUNDING]

# set if a scan ever asked for a difference further out than its timing's window holds. `_reach` is
# meant to make that impossible, and reading the edge instead costs a held frame rather than a failed
# render - but it means a scan went somewhere the reach analysis didn't account for, so the tests watch it
WINDOW_CLAMPED = False

# anchor times are counted in half frames. CENTER puts a run's anchor halfway along it, which for an even
# length run is halfway through a frame - counting in halves keeps every time an exact whole number
HALF = 2


def _resolution(timing: str) -> int:
    """How many decisions a source frame needs.

    An anchor that lands halfway through a frame splits it in two - the frames before it belong to one pair
    and the frames after it to the next - so CENTER decides twice a frame and no anchor ever falls inside a
    decision. Every other timing anchors on whole frames and needs one.
    """
    return HALF if timing == TIMING_CENTER else 1


def _reach(timing: str, max_gap: int) -> int:
    """How far either side of itself a decision has to read.

    CENTER is the only timing that needs a neighbouring run measured end to end rather than just found, so
    it's the only one that reaches past its own run.
    """
    return (2 * max_gap if timing == TIMING_CENTER else max_gap) + 2


def _decisions(
    clip: vs.VideoNode,
    threshold: float,
    max_gap: int,
    timing: str,
    future_checks: int,
) -> vs.VideoNode:
    """Work out, for every frame of `clip`, which two real frames it sits between and when they belong.

    A frame is a *repeat* when it differs from its predecessor by less than `threshold`, which divides the
    video into runs of identical frames. `timing` decides where on the timeline each run's picture goes, and
    everything below follows from that: the pair for a frame is the anchor at or before it and the next one
    after, and a pair is never allowed to span more than `max_gap` frames - past that the picture holds and
    then moves over the last `max_gap` frames before the change.
    """
    diffs = core.std.PlaneStats(clip, clip[0] + clip)

    reach = _reach(timing, max_gap)
    offsets = list(range(-reach, reach + 1))
    window = [retime.shifted(diffs, offset) for offset in offsets]

    resolution = _resolution(timing)
    if resolution != 1:
        window = [core.std.Interleave([shifted] * resolution) for shifted in window]

    length = clip.num_frames
    last = length - 1

    # the props are the whole point of this clip, so its frames are as small as a frame gets
    holder = core.std.BlankClip(width=1, height=1, format=vs.GRAY8, length=length * resolution, keep=True)

    # window frames come after `holder` in the list handed to the selector
    base = 1 + offsets.index(0)

    def decide(n: int, f: list[vs.VideoFrame]) -> vs.VideoFrame:
        # `n` indexes the decisions, which for CENTER run two to a source frame
        source = n // resolution
        time = n * (HALF // resolution)

        def diff(index: int) -> float:
            wanted = base + index - source
            edge = min(max(wanted, 1), len(f) - 1)

            if edge != wanted:
                global WINDOW_CLAMPED
                WINDOW_CLAMPED = True

            return f[edge].props["PlaneStatsDiff"]  # type: ignore[return-value]

        def run(index: int, back: int, on: int) -> tuple[int, int, bool, bool]:
            """The stretch of identical frames `index` is in, looking no further than it's allowed to.

            The two flags say the stretch carried on past where the scan could look, so the frame that really
            begins or ends it isn't known and nothing should be anchored to it.
            """
            low = max(0, index - back)
            high = min(last, index + on)

            start = index
            while start > low and diff(start) < threshold:
                start -= 1

            end = index
            while end < high and diff(end + 1) < threshold:
                end += 1

            return (
                start,
                end,
                start > 0 and diff(start) < threshold,
                end < last and diff(end + 1) < threshold,
            )

        def held() -> tuple[int, int, int, int, int]:
            """Nothing to move towards, so this frame's own picture is the answer for its whole slot."""
            return source, HALF * source, source, HALF * source, 1

        def pair(left: int, left_time: int, right: int, right_time: int) -> tuple[int, int, int, int, int]:
            # never generate across more than the range allows. clamping the time rather than the frame is
            # what keeps this right: `left`'s picture is the one that belongs for every moment up to where
            # the move starts, so holding it for longer and moving over the last `max_gap` frames is exactly
            # what the setting asks for
            return (
                left,
                max(left_time, right_time - HALF * max_gap),
                right,
                right_time,
                0,
            )

        start, end, open_start, open_end = run(source, max_gap, max_gap)

        if timing == TIMING_FIRST:
            # the picture arrived when the run started, and the next one when the next run started
            answer = held() if open_end or end >= last else pair(start, HALF * start, end + 1, HALF * (end + 1))

        elif timing == TIMING_LAST:
            # the picture arrives as the run ends, so before that we're still moving towards it
            if source < end:
                # the picture before this run is the one that belongs here, so its run has to be in
                # reach - `open_start` means it isn't, and the frame before `start` would then be this
                # run's own picture rather than the one it's still moving away from
                answer = (
                    held()
                    if open_end or open_start
                    else pair(max(start - 1, 0), HALF * max(start - 1, 0), end, HALF * end)
                )
            elif end >= last:
                answer = held()
            else:
                _, next_end, _, next_open = run(end + 1, 0, max_gap)
                answer = held() if next_open else pair(end, HALF * end, next_end, HALF * next_end)

        elif timing == TIMING_CENTER:
            # halfway along the run, which needs both of its ends known
            if open_start or open_end:
                answer = held()
            elif time >= start + end:
                if end >= last:
                    answer = held()
                else:
                    next_start, next_end, _, next_open = run(end + 1, 0, max_gap)
                    answer = held() if next_open else pair(start, start + end, next_start, next_start + next_end)
            elif start <= 0:
                answer = held()
            else:
                prev_start, prev_end, prev_open, _ = run(start - 1, max_gap, 0)
                answer = held() if prev_open else pair(prev_start, prev_start + prev_end, start, start + end)

        else:  # TIMING_SURROUNDING
            # a run of one frame isn't in question - both readings put its picture at its own index - so
            # it anchors itself. A longer one is stepped over, and the frames either side carry the gap.
            lone = start == end
            unreachable = open_end or end >= last or (not lone and (open_start or start <= 0))

            if unreachable:
                answer = held()
            else:
                left = source if lone else start - 1
                right = end + 1

                # ...and a run the search lands on is in question the same way, so step over that too,
                # as many times as allowed and as far as the range has room for. this is what makes the
                # timing come out right whichever end of a run the picture really belongs to: every
                # frame it ends up working from is one that both readings agree about
                for _ in range(future_checks):
                    if right >= last:
                        break

                    # a run can only be stepped over if what's on the far side of it still fits the
                    # range, so there's no reason to measure it any further than that - and, since the
                    # window is sized for the range, no room to
                    room = max(0, left + max_gap - 1 - right)

                    _, next_end, _, next_open = run(right, 0, room)

                    # stop on a run of one - its timing isn't in question, so it's what the search was
                    # looking for - and on one there's no room to step over, or nothing to step onto
                    if next_end == right or next_open or next_end >= last or next_end + 1 - left > max_gap:
                        break

                    right = next_end + 1

                answer = held() if right - left > max_gap else pair(left, HALF * left, right, HALF * right)

        left, left_time, right, right_time, hold = answer

        out = f[0].copy()
        out.props[retime.PROP_FRAMES] = [left] if hold else [left, right]
        out.props[retime.PROP_TIMES] = [left_time] if hold else [left_time, right_time]
        out.props[retime.PROP_TIME_SCALE] = HALF
        out.props[retime.PROP_DIFF] = diff(source)

        return out

    return core.std.ModifyFrame(holder, [holder, *window], decide)


def analyse(
    clip: vs.VideoNode,
    threshold: float,
    max_gap: int | None,
    timing: str = TIMING_FIRST,
    future_checks: int = 0,
) -> retime.Timeline:
    """Set up deduplication for `clip`, without reading any of it yet."""
    if timing not in TIMINGS:
        raise u.BlurException(f"Deduplicate real frame must be one of: {', '.join(TIMINGS)}")

    future_checks = max(0, int(future_checks))

    if max_gap is None:
        log.info(f"deduplication: unlimited range, capped at {retime.MAX_GAP_LIMIT} frames")
        max_gap = retime.MAX_GAP_LIMIT
    else:
        max_gap = max(1, min(int(max_gap), retime.MAX_GAP_LIMIT))

    where = (
        "working from the frames either side of a run"
        if timing == TIMING_SURROUNDING
        else f"{timing} frame of a run is the real one"
    )
    log.info(f"deduplicating (threshold {threshold}, up to {max_gap} frames apart, {where})")

    return retime.Timeline(
        decisions=_decisions(clip, threshold, max_gap, timing, future_checks),
        length=clip.num_frames,
        max_gap=max_gap,
        resolution=_resolution(timing),
    )


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
    filldrops = core.mv.FlowInter(clip, super, mvbw=backwards_vectors, mvfw=forward_vectors, ml=1)

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
