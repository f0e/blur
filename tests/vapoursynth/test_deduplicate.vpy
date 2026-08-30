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
    """`unique` distinct values, each repeated as many times as `holds` says (cycling)."""
    values, positions, index = [], [], 0
    for i in range(unique):
        hold = holds[i % len(holds)]
        positions.append(index)
        values += [(i + 1) * 40 % 256] * hold
        index += hold

    return values, positions


VALUES, _ = duplicated(10, [2, 2, 3, 1, 2, 4])
VALUES = [0, 40] + VALUES + [7] * 12 + [90, 130]  # a long still stretch in the middle
LENGTH = len(VALUES)
VIDEO = grey_clip(VALUES)


# ---------------------------------------------------- the rule, in plain python


def ref_diffs(values):
    return [0.0] + [abs(values[n] - values[n - 1]) / 255 for n in range(1, len(values))]


def ref_decisions(values, threshold, max_gap):
    diffs = ref_diffs(values)
    last = len(values) - 1

    def real(i):
        return i == 0 or diffs[i] >= threshold

    out = []
    for n in range(len(values)):
        back = next((i for i in range(n, max(n - max_gap, 0) - 1, -1) if real(i)), None)
        forward = next(
            (i for i in range(n + 1, min(n + max_gap, last) + 1) if real(i)), None
        )

        if forward is None:
            out.append((n, n, True))
        else:
            start = forward - max_gap
            out.append((start if back is None else max(back, start), forward, False))

    return out


def read(dedupe, n):
    props = dedupe.decisions.get_frame(n).props
    return (
        int(props[dd.PROP_LEFT]),
        int(props[dd.PROP_RIGHT]),
        bool(props[dd.PROP_HOLD]),
    )


# --------------------------------------------------------------- 1. decisions

for max_gap in (1, 2, 3, 5, 8, 13):
    dedupe = dd.analyse(VIDEO, threshold=THRESHOLD, max_gap=max_gap)
    want = ref_decisions(VALUES, THRESHOLD, max_gap)

    for n in range(LENGTH):
        check(f"decision[gap={max_gap}][{n}]", read(dedupe, n), want[n])

DEDUPE = dd.analyse(VIDEO, threshold=THRESHOLD, max_gap=5)

for n in range(LENGTH):
    left, right, hold = read(DEDUPE, n)

    if hold:
        if (left, right) != (n, n):
            fail(f"held frame {n} points at ({left}, {right}) instead of itself")
        continue

    if not left <= n < right:
        fail(f"frame {n} isn't inside its own pair ({left}, {right})")

    if right - left > DEDUPE.max_gap:
        fail(f"pair ({left}, {right}) for frame {n} is wider than the range")

    # the interpolator is handed each pair once, indexed by the frame it starts from, so a pair's own
    # start frame has to work out that same pair - otherwise the wrong frames get interpolated
    check(
        f"pair is self-consistent at {left} (from {n})",
        read(DEDUPE, left),
        (left, right, False),
    )

    # a pair must end on a frame that really is new, or there'd be nothing to move towards
    if ref_diffs(VALUES)[right] < THRESHOLD:
        fail(f"pair ({left}, {right}) for frame {n} ends on a duplicate")


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


for target in (Fraction(300), Fraction(60), Fraction(1200), Fraction(150)):
    ratio = target / SRC_FPS
    retimed = it._retimed(VIDEO, DEDUPE, target, counting_interpolator)
    steps = it._steps(ratio, DEDUPE.max_gap)

    check(f"output length @{target}", retimed.num_frames, int(LENGTH * ratio))
    check(f"output fps @{target}", retimed.fps, target)

    for n, frame in enumerate(retimed.frames(close=True)):
        time = Fraction(n) / ratio
        at = dd.bracket(
            DEDUPE.decisions.get_frame(min(int(time), LENGTH - 1)).props, time
        )
        got = dict(frame.props)

        if at.timepoint is None:
            check(f"held @{target}[{n}]", got.get("SrcIndex"), at.left)
        else:
            check(f"pair @{target}[{n}]", got.get("PairIndex"), 2 * at.left)
            check(
                f"step @{target}[{n}]",
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
TRUTH_MAX_GAP = 5


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


# ------------------------------- 4. the debug overlay marks the right frames

# only frames deduplication had a hand in should get written on: not the ones between two frames that
# were both really captured, which are just ordinary interpolation
for target in (Fraction(300), Fraction(60)):
    ratio = target / SRC_FPS
    diffs = ref_diffs(VALUES)

    for n in range(int(LENGTH * ratio)):
        time = Fraction(n) / ratio
        source = min(int(time), LENGTH - 1)
        at = dd.bracket(DEDUPE.decisions.get_frame(source).props, time)

        # a frame is deduplication's doing when the picture is being held, or when its pair spans frames
        # that were dropped - which is exactly when the frame it sits on is a repeat, or the next one is
        held = at.left == at.right
        spans_duplicates = not held and any(
            diffs[i] < THRESHOLD for i in range(at.left + 1, at.right + 1)
        )

        check(f"marked @{target}[{n}]", dd.involved(at), held or spans_duplicates)

MOVING_ALL = grey_clip([(n * 20) % 256 for n in range(30)])
MOVING_ALL_DEDUPE = dd.analyse(MOVING_ALL, THRESHOLD, 5)
marked = sum(
    dd.involved(
        dd.bracket(MOVING_ALL_DEDUPE.decisions.get_frame(n).props, Fraction(n))
    )
    for n in range(30)
)
# only the last frame, which has nothing after it to move towards
check("frames marked in a clip with no duplicates", marked, 1)


# ------------------------------------------------------------- 5. odd clips

check(
    "single frame", dd.analyse(grey_clip([100]), THRESHOLD, 5).decisions.num_frames, 1
)

STATIC = grey_clip([100] * 20)
STATIC_DEDUPE = dd.analyse(STATIC, THRESHOLD, 5)
for n in range(20):
    if not STATIC_DEDUPE.decisions.get_frame(n).props[dd.PROP_HOLD]:
        fail(f"static frame {n} isn't held")

static_out = it._retimed(STATIC, STATIC_DEDUPE, Fraction(300), counting_interpolator)
for n, frame in enumerate(static_out.frames(close=True)):
    if "SrcIndex" not in frame.props:
        fail(f"static output frame {n} was interpolated instead of held")

MOVING = grey_clip([(n * 20) % 256 for n in range(20)])
MOVING_DEDUPE = dd.analyse(MOVING, THRESHOLD, 5)
for n in range(19):  # the last frame has nothing after it, so it holds
    left, right, hold = read(MOVING_DEDUPE, n)
    if hold or (left, right) != (n, n + 1):
        fail(
            f"frame {n} of a clip with no duplicates got ({left}, {right}, hold={hold})"
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
