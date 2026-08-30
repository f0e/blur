"""Tests for deduplication and the retiming it drives.

The maths here is all frame indices and time points, which is exactly the sort of thing that looks right and
isn't, so it's worth being able to check. Nothing here needs a video file: sources are built out of BlankClip,
and the interpolator is stubbed out, so a run takes about a second.

Run it with the vapoursynth that ships in the built app - from this folder, on macos:

    RES=../../bin/Release/Blur.app/Contents/Resources
    PYTHONHOME=$RES/python $RES/vapoursynth/vspipe -p test_deduplicate.vpy /dev/null

It prints a pass/fail summary to stderr and exits after writing a single blank frame.
"""

import sys
from fractions import Fraction
from pathlib import Path

import vapoursynth as vs
from vapoursynth import core

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(1, str(REPO / "src" / "vapoursynth"))

import blur.deduplicate as dd  # noqa: E402
import blur.interpolate as it  # noqa: E402

FAILURES: list[str] = []
CHECKS = 0


def check(name, got, want):
    global CHECKS
    CHECKS += 1
    if got != want:
        FAILURES.append(f"{name}: got {got!r}, want {want!r}")


def fail(message):
    global CHECKS
    CHECKS += 1
    FAILURES.append(message)


SRC_FPS = Fraction(60)
THRESHOLD = 0.05


# ---------------------------------------------------------------- sources


def grey_clip(values):
    """One flat frame per value, so two frames are duplicates exactly when their values match."""
    frames = [
        core.std.BlankClip(
            width=8, height=8, format=vs.GRAY8, length=1, color=value, keep=True
        )
        for value in values
    ]

    clip = core.std.AssumeFPS(
        core.std.Splice(frames), fpsnum=SRC_FPS.numerator, fpsden=SRC_FPS.denominator
    )

    def tag(n, f):
        out = f.copy()
        out.props["SrcIndex"] = n
        return out

    return core.std.ModifyFrame(clip, clip, tag)


def duplicated(unique, holds):
    values, index = [], 0
    for i in range(unique):
        hold = holds[i % len(holds)]
        values += [(i + 1) * 40 % 256] * hold
        index += hold

    return values


# the long still stretch is reached over a run of two, so a search that steps over that run lands
# on a stretch far longer than it - which is the shape that used to read past the window
# ...and it ends inside a run rather than on a lone frame, which is what a recording trimmed at an
# arbitrary point looks like, and what a search stepping forward can run off the end of
VALUES = (
    [0, 40]
    + duplicated(10, [2, 2, 3, 1, 2, 4])
    + [144]
    + [7] * 12
    + [90, 130, 130, 130]
)
LENGTH = len(VALUES)
VIDEO = grey_clip(VALUES)


# ------------------------------------------- the timeline, worked out the slow way


def ref_diffs(values):
    return [0.0] + [abs(values[n] - values[n - 1]) / 255 for n in range(1, len(values))]


def ref_runs(values, threshold):
    """The stretches of identical frames, in one pass over the whole clip."""
    diffs = ref_diffs(values)
    runs, start = [], 0
    for i in range(1, len(values) + 1):
        if i == len(values) or diffs[i] >= threshold:
            runs.append((start, i - 1))
            start = i

    return runs


def ref_anchors(values, threshold, timing):
    """Where each run's picture belongs on the timeline, in half frames, and the frame that shows it.

    Worked out from the whole clip at once, which is what makes this worth checking against: the decisions
    only ever look a few frames either side of themselves and have to reach the same answer.
    """
    anchors = []
    for start, end in ref_runs(values, threshold):
        if timing == dd.TIMING_FIRST:
            anchors.append((start, dd.HALF * start))
        elif timing == dd.TIMING_LAST:
            anchors.append((end, dd.HALF * end))
        elif timing == dd.TIMING_CENTER:
            anchors.append((start, start + end))

    return anchors


def ref_run_at(values, threshold, frame):
    return next(r for r in ref_runs(values, threshold) if r[0] <= frame <= r[1])


def ref_surrounding(values, threshold, max_gap, time, future_checks):
    """`surrounding`'s pair for `time`: the frames either side of the run it lands in."""
    runs = ref_runs(values, threshold)
    last = len(values) - 1
    start, end = ref_run_at(values, threshold, int(time))

    if end >= last:
        return None

    if not (start == end) and start <= 0:
        return None

    left = start if start == end else start - 1
    right = end + 1

    for _ in range(future_checks):
        if right >= last:
            break

        following = next(r for r in runs if r[0] <= right <= r[1])
        if following[0] == following[1] or following[1] - right > max_gap:
            break
        if following[1] + 1 - left > max_gap:
            break

        right = following[1] + 1

    if right - left > max_gap:
        return None

    return left, right, Fraction(max(left, right - max_gap)), Fraction(right)


def ref_bracket(values, threshold, timing, max_gap, time, future_checks=0):
    """The pair the reference timeline puts around `time`, or None where it can't say."""
    if timing == dd.TIMING_SURROUNDING:
        return ref_surrounding(values, threshold, max_gap, time, future_checks)

    anchors = ref_anchors(values, threshold, timing)
    half = time * dd.HALF

    before = [a for a in anchors if a[1] <= half]
    after = [a for a in anchors if a[1] > half]
    if not before or not after:
        return None

    (left, left_time), (right, right_time) = before[-1], after[0]
    left_time = max(left_time, right_time - dd.HALF * max_gap)

    return left, right, Fraction(left_time, dd.HALF), Fraction(right_time, dd.HALF)


def read(dedupe, j, time=None):
    """The bracket decision `j` gives, for the moment that decision starts at unless told otherwise."""
    props = dedupe.decisions.get_frame(j).props

    return dd.bracket(props, Fraction(j, dedupe.resolution) if time is None else time)


# --------------------------------------------------------------- 1. decisions

# wherever the whole-clip reference can answer, the windowed decisions must reach the same pair
for timing in dd.TIMINGS:
    for max_gap in (3, 5, 8):
        dedupe = dd.analyse(VIDEO, threshold=THRESHOLD, max_gap=max_gap, timing=timing)

        for j in range(dedupe.decisions.num_frames):
            time = Fraction(j, dedupe.resolution)
            at = read(dedupe, j)
            want = ref_bracket(VALUES, THRESHOLD, timing, max_gap, time)

            if want is None or at.timepoint is None:
                continue

            # the frames only have to be the same picture: inside a run longer than the decision can
            # see, it names one further along than the run really starts, which shows the same thing
            check(
                f"bracket[{timing}][gap={max_gap}][{j}]",
                (
                    ref_run_at(VALUES, THRESHOLD, at.left),
                    ref_run_at(VALUES, THRESHOLD, at.right),
                    at.left_time,
                    at.right_time,
                ),
                (
                    ref_run_at(VALUES, THRESHOLD, want[0]),
                    ref_run_at(VALUES, THRESHOLD, want[1]),
                    want[2],
                    want[3],
                ),
            )

# stepping over runs that are themselves in question
for future_checks in (0, 1, 2, 5):
    dedupe = dd.analyse(
        VIDEO,
        threshold=THRESHOLD,
        max_gap=8,
        timing=dd.TIMING_SURROUNDING,
        future_checks=future_checks,
    )

    for j in range(dedupe.decisions.num_frames):
        time = Fraction(j)
        at = read(dedupe, j)
        want = ref_bracket(
            VALUES, THRESHOLD, dd.TIMING_SURROUNDING, 8, time, future_checks
        )

        if want is None or at.timepoint is None:
            continue

        check(
            f"surrounding[checks={future_checks}][{j}]",
            (at.left, at.right, at.left_time, at.right_time),
            want,
        )

# more checks can only ever reach the same pair or a wider one
WIDTHS = []
for future_checks in (0, 3):
    dedupe = dd.analyse(
        VIDEO,
        threshold=THRESHOLD,
        max_gap=8,
        timing=dd.TIMING_SURROUNDING,
        future_checks=future_checks,
    )
    WIDTHS.append(
        [read(dedupe, j).right_time - read(dedupe, j).left_time for j in range(LENGTH)]
    )

if not all(b >= a for a, b in zip(*WIDTHS)):
    fail("future checks narrowed a pair instead of widening it")
if WIDTHS[0] == WIDTHS[1]:
    fail(
        "future checks made no difference at all - the test source has nothing to step over"
    )


# each timing must place a run's picture where it says it does
RUNS = [0, 40, 40, 40, 80, 120, 120, 160, 200, 200, 200, 200, 240]
RUNS_CLIP = grey_clip(RUNS)
PLACED = {timing: dd.analyse(RUNS_CLIP, THRESHOLD, 8, timing) for timing in dd.TIMINGS}

# 40 runs over 1..3, 120 over 5..6, 200 over 8..11; 0, 4, 7 and 12 stand alone
for run_start, run_end in ((1, 3), (5, 6), (8, 11)):
    check(
        f"first anchors {run_start}-{run_end} at its start",
        read(PLACED[dd.TIMING_FIRST], run_start).left_time,
        run_start,
    )
    check(
        f"last anchors {run_start}-{run_end} at its end",
        read(PLACED[dd.TIMING_LAST], run_end).left_time,
        run_end,
    )

    # center puts it halfway along, which for an even length run is halfway through a frame
    middle = Fraction(run_start + run_end, 2)
    centered = PLACED[dd.TIMING_CENTER]
    check(
        f"center anchors {run_start}-{run_end} halfway",
        read(centered, int(middle * centered.resolution), middle).left_time,
        middle,
    )

    # surrounding skips the run and works from the frames either side of it
    at = read(PLACED[dd.TIMING_SURROUNDING], run_start)
    check(
        f"surrounding brackets {run_start}-{run_end} from outside",
        (at.left, at.right),
        (run_start - 1, run_end + 1),
    )

# a run of one frame isn't in question, so every timing anchors it on itself
for lone in (4, 7):
    for timing, dedupe in PLACED.items():
        at = read(dedupe, lone * dedupe.resolution, Fraction(lone))
        check(f"{timing} anchors the lone frame {lone}", at.left_time, lone)

# invariants that have to hold whatever the timing
DEDUPE = dd.analyse(VIDEO, threshold=THRESHOLD, max_gap=5)
REF_RUNS = ref_runs(VALUES, THRESHOLD)


def containing_run(frame):
    return next(r for r in REF_RUNS if r[0] <= frame <= r[1])


for timing in dd.TIMINGS:
    checked = dd.analyse(VIDEO, threshold=THRESHOLD, max_gap=5, timing=timing)

    for j in range(checked.decisions.num_frames):
        time = Fraction(j, checked.resolution)
        at = read(checked, j)

        if at.timepoint is None:
            continue

        if not at.left_time <= time < at.right_time:
            fail(
                f"[{timing}] {time} sits outside its own pair {at.left_time}-{at.right_time}"
            )

        if at.right_time - at.left_time > checked.max_gap:
            fail(f"[{timing}] the pair at {time} spans more than the range")

        if not 0 < at.timepoint < 1:
            fail(f"[{timing}] time point {at.timepoint} at {time} is outside the pair")

        # a pair has to span a real change - two frames out of the same run are the same picture,
        # and generating between them would be generating nothing
        if containing_run(at.left) == containing_run(at.right):
            fail(
                f"[{timing}] the pair at {time} takes both frames from the run "
                f"{containing_run(at.left)}"
            )


# ------------------------------------------------------- 2. retiming indices


def counting_interpolator(pairs, steps):
    """Stands in for an interpolator: every frame records which pair and step it came from."""
    out = core.std.BlankClip(pairs, length=pairs.num_frames * steps, keep=True)

    def mark(n, f):
        frame = f.copy()
        frame.props["PairIndex"] = n // steps
        frame.props["Step"] = n % steps
        return frame

    return core.std.ModifyFrame(out, out, mark)


for timing in dd.TIMINGS:
    dedupe = dd.analyse(VIDEO, threshold=THRESHOLD, max_gap=5, timing=timing)

    for target in (Fraction(300), Fraction(60), Fraction(1200), Fraction(150)):
        ratio = target / SRC_FPS
        retimed = it._retimed(VIDEO, dedupe, target, counting_interpolator)
        steps = it._steps(ratio, dedupe.max_gap)

        check(
            f"[{timing}] output length @{target}",
            retimed.num_frames,
            int(LENGTH * ratio),
        )
        check(f"[{timing}] output fps @{target}", retimed.fps, target)

        for n, frame in enumerate(retimed.frames(close=True)):
            time = Fraction(n) / ratio
            j = dd.decision_index(dedupe, n, ratio)
            at = read(dedupe, j, time)
            got = dict(frame.props)

            if at.timepoint is None:
                check(f"[{timing}] held @{target}[{n}]", got.get("SrcIndex"), at.left)
            else:
                check(f"[{timing}] pair @{target}[{n}]", got.get("PairIndex"), 2 * j)
                check(
                    f"[{timing}] step @{target}[{n}]",
                    got.get("Step"),
                    min(round(at.timepoint * steps), steps),
                )

# output frames are spread evenly over real time however the duplicates fell, so no stretch of the
# video ends up with fewer frames - and so no stretch gets fewer blur samples than another
ratio = Fraction(300) / SRC_FPS
retimed = it._retimed(VIDEO, DEDUPE, Fraction(300), counting_interpolator)
density = [0] * LENGTH
for n in range(retimed.num_frames):
    density[int(Fraction(n) / ratio)] += 1
check("output frames per source frame", set(density), {int(ratio)})


# ------------------------------------------------- 3. timing, against ground truth
#
# brightness *is* the timestamp here - a frame captured at source index p has value p/1000 - so with a
# perfect interpolator (a blend, which is exact for content that changes linearly) any error in the
# retiming shows up as an error in brightness.

TRUTH_HOLDS = [2, 3, 1, 4]
TRUTH_MAX_GAP = 6


def truth_clip():
    values, positions, index = [], [], 0
    for i in range(30):
        hold = TRUTH_HOLDS[i % len(TRUTH_HOLDS)]
        positions.append(index)
        values += [index] * hold
        index += hold

    frames = [
        core.std.BlankClip(
            width=8, height=8, format=vs.GRAYS, length=1, color=value / 1000, keep=True
        )
        for value in values
    ]

    clip = core.std.AssumeFPS(
        core.std.Splice(frames), fpsnum=SRC_FPS.numerator, fpsden=SRC_FPS.denominator
    )

    return clip, positions


TRUTH, TRUTH_POSITIONS = truth_clip()


def blending_interpolator(pairs, steps):
    """A perfect interpolator, for content that changes linearly: a straight blend."""
    out = core.std.BlankClip(pairs, length=pairs.num_frames * steps, keep=True)

    def generate(n):
        pair, step = n // steps, n % steps
        before = pairs[pair]
        after = pairs[min(pair + 1, pairs.num_frames - 1)]

        return before if step == 0 else core.std.Merge(before, after, step / steps)

    return core.std.FrameEval(out, generate)


def brightness(frame):
    import struct

    return struct.unpack("f", frame[0].tobytes()[:4])[0]


# `first` is the timing this source was built to match: every run holds the picture that started it,
# so it's the one that should land every frame exactly where its timestamp says
for target in (Fraction(300), Fraction(240), Fraction(60), Fraction(1200)):
    dedupe = dd.analyse(TRUTH, threshold=0.0005, max_gap=TRUTH_MAX_GAP)
    ratio = target / SRC_FPS
    out = it._retimed(TRUTH, dedupe, target, blending_interpolator)

    # past the last frame that isn't a repeat there's nothing to move towards, so the tail holds
    last_real = TRUTH_POSITIONS[-1]

    # all that should be left is the rounding to the nearest step, which _steps keeps well under an
    # output frame
    budget = float(1 / ratio) / it.STEPS_PER_OUTPUT_FRAME * 1.01

    worst = 0.0
    previous = None
    for n, frame in enumerate(out.frames(close=True)):
        time = Fraction(n) / ratio
        if time > last_real:
            break

        error = abs(brightness(frame) - float(time) / 1000) * 1000  # in source frames
        worst = max(worst, error)

        # and it should never go backwards or stall - that's the stutter this is all here to remove
        if previous is not None and brightness(frame) <= previous:
            fail(f"output @{target} stalled or went backwards at frame {n}")
        previous = brightness(frame)

    CHECKS += 1
    if worst > budget:
        FAILURES.append(
            f"timing @{target}: worst error {worst:.5f} source frames, budget {budget:.5f}"
        )

# The point of `surrounding` and `center` is what they do when the footage doesn't match the reading
# you picked, so both get built twice: the same motion recorded once with each picture held forward
# from the frame that started its run, and once held back to the frame that ends it.

SHAPE = [2, 1, 3, 1, 4, 1, 2, 1, 3, 1, 2, 1]
SHAPE_GAP = 8
SHAPE_CHECKS = 4


def hypothesis_clip(holds, held_late):
    """Content moving at a steady rate, recorded with drops. Brightness is the time it belongs at."""
    values, position = [], 0
    for hold in holds:
        values += [position + hold - 1 if held_late else position] * hold
        position += hold

    frames = [
        core.std.BlankClip(
            width=8, height=8, format=vs.GRAYS, length=1, color=value / 1000, keep=True
        )
        for value in values
    ]

    return core.std.AssumeFPS(
        core.std.Splice(frames), fpsnum=SRC_FPS.numerator, fpsden=SRC_FPS.denominator
    ), len(values)


def worst_error(clip, dedupe, target, window):
    """How far the retimed output lands from where each frame's timestamp says it should, in frames.

    `window` cuts off the first and last run: there's nothing before the one and nothing after the
    other, so no reading can place them and every timing has to hold.
    """
    ratio = target / SRC_FPS
    out = it._retimed(clip, dedupe, target, blending_interpolator)
    low, high = window

    worst = 0.0
    for n, frame in enumerate(out.frames(close=True)):
        time = Fraction(n) / ratio
        if not low <= time <= high:
            continue

        at = read(dedupe, dd.decision_index(dedupe, n, ratio), time)

        # only where it committed to a pair; a held stretch isn't claiming to be anywhere
        if at.timepoint is None:
            continue

        worst = max(worst, abs(brightness(frame) - float(time) / 1000) * 1000)

    return worst


TARGET = Fraction(300)
BUDGET = float(SRC_FPS / TARGET) / it.STEPS_PER_OUTPUT_FRAME * 1.01

for held_late in (False, True):
    clip, total = hypothesis_clip(SHAPE, held_late)
    reading = "held late" if held_late else "held early"
    window = (Fraction(SHAPE[0]), Fraction(total - 1 - SHAPE[-1]))

    errors = {}
    for timing in dd.TIMINGS:
        dedupe = dd.analyse(
            clip,
            threshold=0.0005,
            max_gap=SHAPE_GAP,
            timing=timing,
            future_checks=SHAPE_CHECKS,
        )
        errors[timing] = worst_error(clip, dedupe, TARGET, window)

    # the reading that matches the footage is exact, and `surrounding` is exact either way - it only
    # ever works from frames both readings agree about
    matching = dd.TIMING_LAST if held_late else dd.TIMING_FIRST
    for timing in (matching, dd.TIMING_SURROUNDING):
        CHECKS += 1
        if errors[timing] > BUDGET:
            FAILURES.append(
                f"[{reading}] {timing} was out by {errors[timing]:.4f} frames, budget {BUDGET:.4f}"
            )

    # and `center` splits the difference: never worse than half of what picking the wrong end costs
    wrong = dd.TIMING_FIRST if held_late else dd.TIMING_LAST
    CHECKS += 1
    if not errors[dd.TIMING_CENTER] <= errors[wrong] / 2 + BUDGET:
        FAILURES.append(
            f"[{reading}] center was out by {errors[dd.TIMING_CENTER]:.4f} frames, "
            f"more than half of {wrong}'s {errors[wrong]:.4f}"
        )

    CHECKS += 1
    if errors[wrong] <= BUDGET:
        FAILURES.append(
            f"[{reading}] {wrong} should have been wrong here but wasn't - the source shape "
            "isn't telling the readings apart"
        )


# ------------------------------- 4. the debug overlay marks the right frames

# only frames deduplication had a hand in should get written on: not the ones between two frames that
# were both really captured, which are just ordinary interpolation
DIFFS = ref_diffs(VALUES)
for target in (Fraction(300), Fraction(60)):
    ratio = target / SRC_FPS

    for n in range(int(LENGTH * ratio)):
        time = Fraction(n) / ratio
        at = read(DEDUPE, dd.decision_index(DEDUPE, n, ratio), time)

        held = at.left == at.right
        spans_duplicates = not held and any(
            DIFFS[i] < THRESHOLD for i in range(at.left + 1, at.right + 1)
        )

        check(f"marked @{target}[{n}]", dd.involved(at), held or spans_duplicates)

MOVING_ALL = grey_clip([(n * 20) % 256 for n in range(30)])
MOVING_ALL_DEDUPE = dd.analyse(MOVING_ALL, THRESHOLD, 5)
marked = sum(dd.involved(read(MOVING_ALL_DEDUPE, n)) for n in range(30))
# only the last frame, which has nothing after it to move towards
check("frames marked in a clip with no duplicates", marked, 1)


# ------------------------------------------------------------- 5. odd clips

check(
    "single frame", dd.analyse(grey_clip([100]), THRESHOLD, 5).decisions.num_frames, 1
)

STATIC = grey_clip([100] * 20)
for timing in dd.TIMINGS:
    static_dedupe = dd.analyse(STATIC, THRESHOLD, 5, timing)
    for j in range(static_dedupe.decisions.num_frames):
        if not static_dedupe.decisions.get_frame(j).props[dd.PROP_HOLD]:
            fail(f"[{timing}] static decision {j} isn't held")

    static_out = it._retimed(
        STATIC, static_dedupe, Fraction(300), counting_interpolator
    )
    for n, frame in enumerate(static_out.frames(close=True)):
        if "SrcIndex" not in frame.props:
            fail(f"[{timing}] static output frame {n} was interpolated instead of held")

# with no duplicates at all every timing should agree: each frame pairs with the next
MOVING = grey_clip([(n * 20) % 256 for n in range(20)])
for timing in dd.TIMINGS:
    moving = dd.analyse(MOVING, THRESHOLD, 5, timing)
    for n in range(19):  # the last frame has nothing after it, so it holds
        at = read(moving, n * moving.resolution, Fraction(n))
        if (at.left, at.right, at.left_time, at.right_time) != (n, n + 1, n, n + 1):
            fail(
                f"[{timing}] frame {n} of a clip with no duplicates got "
                f"({at.left}, {at.right}, {at.left_time}, {at.right_time})"
            )


# ------------------------------------------ 6. the scans stay inside their window

# every scan above ran against sources with runs of one, two, three, four and twelve frames, across
# every timing and a spread of ranges. if any of them reached for a difference the window doesn't
# hold, the reach analysis in `_reach` is wrong for that path - which reads as frames quietly being
# held rather than as anything failing, so nothing else here would notice
CHECKS += 1
if dd.WINDOW_CLAMPED:
    FAILURES.append(
        "a scan read past its window - `_reach` is too small for one of the timings"
    )

# and once more with settings chosen to stretch the scans as far as they go
for timing in dd.TIMINGS:
    for max_gap in (1, 2, 4, 7, 12, 30):
        stretched = dd.analyse(VIDEO, THRESHOLD, max_gap, timing, future_checks=10)
        for j in range(stretched.decisions.num_frames):
            read(stretched, j)

CHECKS += 1
if dd.WINDOW_CLAMPED:
    FAILURES.append(
        "a scan read past its window once the range and future checks were opened up"
    )

# and a pair has to name frames that exist, whatever the settings and wherever in the clip it falls -
# a search that walks off either end shows up here rather than as a failed render
for timing in dd.TIMINGS:
    for max_gap in (1, 2, 4, 7, 12, 30):
        for checks in (0, 1, 4, 10):
            bounded = dd.analyse(VIDEO, THRESHOLD, max_gap, timing, checks)
            for j in range(bounded.decisions.num_frames):
                at = read(bounded, j)
                if not (0 <= at.left < LENGTH and 0 <= at.right < LENGTH):
                    fail(
                        f"[{timing}][gap={max_gap}][checks={checks}] decision {j} names frames "
                        f"({at.left}, {at.right}) outside a clip of {LENGTH}"
                    )


# ------------------------------------------------- 7. clips shorter than the window

# trimming a render to a short region leaves a clip shorter than the range wants to look at, so the
# shifted clips the window is built from collapse onto each other at the edges. every scan has to stay
# inside the clip for that to stay harmless.
SHORT_SHAPES = [
    [10],
    [10, 10],
    [10, 90],
    [10, 10, 10],
    [10, 90, 90],
    [10, 90, 90, 170],
    [10, 10, 90, 90, 90],
    [10, 90, 90, 170, 170, 170, 250, 250],
    [10, 10, 90, 170, 170, 250, 250, 250, 10, 10, 10, 90, 90, 170, 250, 250, 250],
]

for shape in SHORT_SHAPES:
    short = grey_clip(shape)

    for timing in dd.TIMINGS:
        for max_gap in (1, 3, 8, 30):
            for checks in (0, 3, 10):
                try:
                    tiny = dd.analyse(short, THRESHOLD, max_gap, timing, checks)
                    for j in range(tiny.decisions.num_frames):
                        at = read(tiny, j)
                        CHECKS += 1
                        if not (
                            0 <= at.left < len(shape) and 0 <= at.right < len(shape)
                        ):
                            FAILURES.append(
                                f"[{timing}][len={len(shape)}][gap={max_gap}][checks={checks}] "
                                f"decision {j} names ({at.left}, {at.right})"
                            )
                except Exception as e:
                    fail(
                        f"[{timing}][len={len(shape)}][gap={max_gap}][checks={checks}] "
                        f"raised {type(e).__name__}: {e}"
                    )

CHECKS += 1
if dd.WINDOW_CLAMPED:
    FAILURES.append("a scan read past its window on a clip shorter than the window")

# and a short clip still renders end to end
for timing in dd.TIMINGS:
    short = grey_clip(SHORT_SHAPES[-1])
    tiny = dd.analyse(short, THRESHOLD, 8, timing, 4)
    rendered = it._retimed(short, tiny, Fraction(300), counting_interpolator)
    CHECKS += 1
    try:
        for _ in rendered.frames(close=True):
            pass
    except Exception as e:
        FAILURES.append(
            f"[{timing}] a short clip failed to render: {type(e).__name__}: {e}"
        )


# --------------------------------------------------------------- report

if FAILURES:
    print(f"\n{len(FAILURES)}/{CHECKS} checks FAILED:", file=sys.stderr)
    for failure in FAILURES[:40]:
        print(f"  {failure}", file=sys.stderr)
    if len(FAILURES) > 40:
        print(f"  ... and {len(FAILURES) - 40} more", file=sys.stderr)
else:
    print(f"\nall {CHECKS} checks passed", file=sys.stderr)

core.std.BlankClip(format=vs.GRAY8, length=1).set_output()
