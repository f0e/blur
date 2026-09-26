"""Deduplication - working out a timeline from the pictures alone.

A frame that differs from the one before by less than `threshold` is a repeat, which splits the video into runs
of identical frames. Where each run's real picture belongs can't be known from the video, so `timing` picks a
guess. The result is a timeline for blur/retime.py. A frame timing log (blur/frame_timing.py) replaces this.
"""

import blur.utils as u
import vapoursynth as vs
from blur import log, retime
from vapoursynth import core

# which frame of a run of repeats is the real one. only matters when run lengths vary
#
#   FIRST        the start of the run, what a live recording does
#   LAST         the end of the run, e.g. a variable rate recording resampled to a fixed one
#   CENTER       the middle of the run, at most half a run out either way
#   SURROUNDING  ignore the run and interpolate between the frames either side of it. needs a bigger range
TIMING_FIRST = "first"
TIMING_LAST = "last"
TIMING_CENTER = "center"
TIMING_SURROUNDING = "surrounding"
TIMINGS = [TIMING_FIRST, TIMING_LAST, TIMING_CENTER, TIMING_SURROUNDING]

# set if a scan reads past its window, which `_reach` should prevent. checked by the tests
WINDOW_CLAMPED = False

# times are in half frames so CENTER's anchors are whole numbers
HALF = 2


def _resolution(timing: str) -> int:
    """How many decisions a source frame needs. CENTER can anchor halfway through a frame so it needs two."""
    return HALF if timing == TIMING_CENTER else 1


def _reach(timing: str, max_gap: int) -> int:
    """How far either side of itself a decision has to read. CENTER measures neighbouring runs so reaches further."""
    return (2 * max_gap if timing == TIMING_CENTER else max_gap) + 2


def _decisions(
    clip: vs.VideoNode,
    threshold: float,
    max_gap: int,
    timing: str,
    future_checks: int,
) -> vs.VideoNode:
    """For every frame of `clip`, which two real frames it sits between and when they belong. Pairs never span
    more than `max_gap` frames, past that the picture holds then moves over the last `max_gap` frames."""
    diffs = core.std.PlaneStats(clip, clip[0] + clip)

    reach = _reach(timing, max_gap)
    offsets = list(range(-reach, reach + 1))
    window = [retime.shifted(diffs, offset) for offset in offsets]

    resolution = _resolution(timing)
    if resolution != 1:
        window = [core.std.Interleave([shifted] * resolution) for shifted in window]

    length = clip.num_frames
    last = length - 1

    # only the props matter
    holder = core.std.BlankClip(width=1, height=1, format=vs.GRAY8, length=length * resolution, keep=True)

    base = 1 + offsets.index(0)

    def decide(n: int, f: list[vs.VideoFrame]) -> vs.VideoFrame:
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
            """The run of identical frames `index` is in. The flags say the run continued past where the scan
            could look."""
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
            return source, HALF * source, source, HALF * source, 1

        def pair(left: int, left_time: int, right: int, right_time: int) -> tuple[int, int, int, int, int]:
            # hold `left` then move over the last `max_gap` frames
            return (
                left,
                max(left_time, right_time - HALF * max_gap),
                right,
                right_time,
                0,
            )

        start, end, open_start, open_end = run(source, max_gap, max_gap)

        if timing == TIMING_FIRST:
            answer = held() if open_end or end >= last else pair(start, HALF * start, end + 1, HALF * (end + 1))

        elif timing == TIMING_LAST:
            if source < end:
                # moving from the previous run's picture towards this one, which needs the previous run in reach
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
            # needs both ends of the run known
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
            # a run of one anchors itself, longer runs are stepped over
            lone = start == end
            unreachable = open_end or end >= last or (not lone and (open_start or start <= 0))

            if unreachable:
                answer = held()
            else:
                left = source if lone else start - 1
                right = end + 1

                # keep stepping over following runs while the range allows
                for _ in range(future_checks):
                    if right >= last:
                        break

                    room = max(0, left + max_gap - 1 - right)

                    _, next_end, _, next_open = run(right, 0, room)

                    # stop on a run of one, or when there's no room to step over
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
    """The original deduplication, kept for the 'old' method. Replaces each duplicate with the halfway point."""
    if not isinstance(clip, vs.VideoNode):
        raise TypeError("This is not a clip")

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
