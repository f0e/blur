"""Frame timing - putting frames back where the game really drew them.

A recording's frames aren't evenly spaced in game time. Each recorded frame shows whichever game frame the
recorder last got hold of: recording a 500fps game at 360fps, sometimes one game frame has gone by since the
previous recorded frame and sometimes two, and a game that stutters or a recorder that falls behind makes it
worse. Steady motion comes out uneven, and interpolating as though the frames were evenly spaced keeps it
that way.

None of that can be read reliably from the picture, so it's read from a log instead - see
blur/frame_timing_log.py for the file itself. Two things in it answer two separate questions.

**Which recorded frames are new pictures** the log simply says: the plugin hashes the picture obs read on
each tick, so a frame whose hash matches the one before it is a repeat. That is measured, not inferred, and
it beats comparing the video's own frames - it looks at the game's picture rather than the composited scene,
so an overlay animating in the corner can't make every frame look new, and it can't be fooled by a lossy
encode either.

**When each picture happened** comes from the game's own frames, when the plugin had permission to trace
them:

- with a game capture, obs's hook copies the game's frames as they're presented, skipping any that come
  sooner than `interval` after its last copy, and a copy becomes readable once the GPU has finished the game
  frame it was queued behind
- with a window capture, the compositor hands obs the window instead: nothing is skipped, a frame becomes
  readable when it reaches the screen, and one that never got there was never captured
- either way the recorded frame shows the last frame readable when obs read it

and each game frame is placed when the game simulated it. Games that report that - through NVIDIA Reflex or
Intel's PresentMon markers - say exactly; for the rest it's taken as the moment the game started the frame,
the same fallback PresentMon uses.

Two numbers in the middle of that aren't in the log: the phase of the hook's skip grid, and how much later
than the probe's flush the picture really counts as read. Both are fitted to the fingerprints - the timing
that contradicts fewest of the picture changes wins - so neither is tuned per machine and no video is read.
The fingerprints can't always tell: a game far enough ahead of the recording shows a new picture on nearly
every read whatever the latency was, and then the read is left exactly as the probe measured it.

The result is when every recorded frame's picture belongs, on the game's timeline, which becomes the timeline
interpolation renders - see blur/retime.py. A frame that shows the same picture as the one before it drops
out, so this takes deduplication's place rather than running alongside it.

Without the game's frames, each new picture is placed at the moment obs read it instead. That handles a
recorder that falls behind, but not a game running faster than the recording.
"""

import vapoursynth as vs
from vapoursynth import core

import bisect
import math
from fractions import Fraction
from pathlib import Path

import numpy as np

import blur.retime as retime
import blur.frame_timing_log as timing_log
from blur import log

# a frame's position is kept to this fraction of a frame
TIME_SCALE = 1 << 16

# a decision spans one frame of the timeline, which can hold a couple of real frames landing partway through it
SLOTS = 3

# how far either side of zero the read latency is looked for, in recorded frames
LATENCY_REACH = 1.0

# how many phases of the hook's skip grid are tried, over the one interval they repeat in
PHASE_STEPS = 16

# how much of the latency search range is allowed to fit the fingerprints just as well before the fit counts
# as saying nothing - see `fit`. measured: a clip the fingerprints really pin down leaves under a percent of
# the range, and one they can't leaves a third or more of it, in several separate pieces
IDENTIFIABLE_SHARE = 0.1

# how many frames the two fitted numbers are fitted over. they're constants, so a stretch of the clip says as
# much about them as all of it, and this keeps the fit's cost off the length of the recording
FIT_FRAMES = 20000

# how far back the game's frames are wanted before the clip starts, in seconds, so that its first frames have
# something to look back at
LEAD = 2.0


def hook_copies(present: np.ndarray, interval: float, phase: float) -> np.ndarray:
    """Which presents the capture hook copied.

    It skips one that comes sooner than `interval` after its last copy, counting the interval on a fixed grid,
    the way OBS's `frame_ready` does. The grid was anchored whenever the hook started, which the log doesn't
    say, so where it sits is `phase`: how far before the first present of `present` the grid last fell, as a
    share of an interval. 1 puts the first present on the grid, which is what assuming the grid started there
    would do - so anything fitted this way has to be fitted against a run of presents starting in the same
    place, since a grid reset part way along re-anchors it.
    """
    if interval <= 0 or len(present) == 0:
        return np.arange(len(present), dtype=np.int64)

    copied = []
    last = present[0] - phase * interval
    for i, t in enumerate(present):
        elapsed = t - last
        if elapsed < interval:
            continue
        last = t if elapsed > interval * 2 else last + interval
        copied.append(i)
    return np.array(copied, dtype=np.int64)


def picture_changes(fingerprint: np.ndarray) -> np.ndarray:
    """Which frames show a new picture: 1 yes, 0 no, -1 not known."""
    changes = np.ones(len(fingerprint), dtype=np.int8)
    known = fingerprint != 0
    changes[~known] = -1
    if len(fingerprint) > 1:
        same = known[1:] & known[:-1] & (fingerprint[1:] == fingerprint[:-1])
        changes[1:][same] = 0
    return changes


def _latency(
    read: np.ndarray, changes: np.ndarray, visible: np.ndarray, reach: float
) -> tuple[int, float, float, float]:
    """The read latency that contradicts the fingerprints least: how many changes it can't account for, the
    latency itself, how wide the run of latencies it was taken from is, and how much of the whole search
    range scores just as well.

    A recorded frame shows the newest copy that had become visible by the time obs read it, give or take a
    fixed offset. There is one: the probe's flush finishing isn't quite when the draw sampled the shared
    texture, and a game frame's last GPU work isn't quite when the hook's copy of it finished. The
    fingerprints say exactly which recorded frames are new pictures, so the offset is chosen to disagree with
    them as rarely as possible.

    Every offset that would change which copy a frame saw is tried - those are the differences between a
    copy's time and a read - and of the offsets that disagree least, the middle of the widest run of them
    wins, which is the one furthest from being a close call. That run is only worth taking a middle of when
    the fingerprints rule enough of the range out, which is what the last number is for - see `fit`.
    """
    frames = len(read)
    low = np.searchsorted(visible, read - reach, "right")
    high = np.searchsorted(visible, read + reach, "right")

    # every (frame, copy) pair whose ordering the latency could flip, in the order the latency reaches them
    of = np.repeat(np.arange(frames), high - low)
    copy = np.concatenate([np.arange(a, b) for a, b in zip(low, high)]) if frames else np.empty(0, np.int64)
    offset = visible[copy] - read[of]
    order = np.argsort(offset, kind="stable")
    of, copy, offset = of[order], copy[order], offset[order]

    shown = low.astype(np.int64) - 1

    # past the newest copy there is nothing newer for a frame to show, so a change there isn't a contradiction,
    # it's the end of what the game's frames cover, and scoring it would count the log running out as bad
    # timing. which frames those are can't depend on the latency being tried, or the running count couldn't be
    # carried from one latency to the next, so the whole range is allowed for
    scorable = (read + reach <= visible[-1]) if len(visible) else np.zeros(frames, dtype=bool)

    # only one of the two ways the model and the fingerprints can differ is impossible: a frame whose picture
    # changed cannot be showing the same copy as the frame before it. the other way round happens for real -
    # the game can draw two frames that look identical, and often does when nothing is moving - so counting
    # that as an error would fit the timing to how still the game was
    def disagrees(i: int) -> int:
        if i <= 0 or i >= frames or changes[i] <= 0 or not scorable[i] or shown[i] < 0 or shown[i - 1] < 0:
            return 0
        return int(shown[i] == shown[i - 1])

    wrong = sum(disagrees(i) for i in range(frames))

    # the score only changes where the latency reaches one of those pairs, so the sweep is a row of intervals,
    # each holding one score over the whole of it
    bounds = [-reach]
    scores = []
    step = 0
    while step < len(offset):
        latency = float(offset[step])
        scores.append(wrong)
        bounds.append(latency)
        while step < len(offset) and offset[step] == latency:
            frame = int(of[step])
            wrong -= disagrees(frame) + disagrees(frame + 1)
            shown[frame] = copy[step]
            wrong += disagrees(frame) + disagrees(frame + 1)
            step += 1
    scores.append(wrong)
    bounds.append(reach)

    # neighbouring intervals scoring the same are one run, so the middle is taken from the whole of it rather
    # than from whichever single interval inside it happens to be widest
    runs: list[list[float]] = []
    for score, begin, end in zip(scores, bounds, bounds[1:]):
        if runs and runs[-1][0] == score:
            runs[-1][2] = end
        else:
            runs.append([float(score), begin, end])

    fewest = min(run[0] for run in runs)
    equal = [run for run in runs if run[0] == fewest]
    widest = max(equal, key=lambda run: run[2] - run[1])
    return int(fewest), (widest[1] + widest[2]) / 2, widest[2] - widest[1], sum(r[2] - r[1] for r in equal)


def fit(
    present: np.ndarray, visible: np.ndarray, read: np.ndarray, changes: np.ndarray, interval: float, fps: Fraction
) -> tuple[float, float, int, bool]:
    """The hook's grid phase and the read latency that fit the fingerprints best, and whether the latency was
    identifiable at all.

    The phase only matters when the hook skips presents at all - a game presenting slower than the interval
    has every frame copied whatever the grid is doing - so it's only looked for when it can make a difference.
    """
    reach = LATENCY_REACH / float(fps)
    skips = len(present) > 1 and float(np.min(np.diff(present))) < interval
    phases = np.linspace(1, 0, PHASE_STEPS, endpoint=False) if skips else np.array([1.0])

    best: tuple[int, float, float, float, float] | None = None
    for phase in phases:
        copied = hook_copies(present, interval, float(phase))
        if len(copied) == 0:
            continue

        wrong, latency, width, span = _latency(read, changes, np.sort(visible[copied]), reach)
        if best is None or (wrong, -width) < (best[0], -best[3]):
            best = (wrong, latency, float(phase), width, span)

    # nothing was copied under any phase, so there's nothing to fit and nothing to fit it to
    if best is None:
        return 1.0, 0.0, 0, False

    # a game outrunning the recording shows a new picture on nearly every read whatever the latency is, so
    # nearly every latency explains the fingerprints equally well and they aren't saying anything about it.
    # the middle of that would be a number picked out of noise, and picking one is worse than not: the read
    # stays as the probe measured it, and the caller says so
    identified = best[4] < IDENTIFIABLE_SHARE * 2 * reach
    return best[2], (best[1] if identified else 0.0), best[0], identified


def game_frames(
    read: np.ndarray, presents: timing_log.Presents, changes: np.ndarray, phase: float, latency: float
) -> np.ndarray:
    """For each read, the game frame it saw. Reads the game's frames don't cover get -1."""
    copied = hook_copies(presents.present, presents.interval, phase)
    if len(copied) == 0:
        return np.full(len(read), -1, dtype=np.int64)

    visible = presents.visible[copied]
    order = np.argsort(visible, kind="stable")
    visible = visible[order]
    copied = copied[order]

    index = np.searchsorted(visible, read + latency, "right") - 1
    covered = index >= 0
    game = np.maximum.accumulate(np.where(covered, copied[np.maximum(index, 0)], -1))

    # past the game's last logged frame there's nothing to say a read saw anything newer, which is right if
    # the game stopped drawing and wrong if the log ran out - the fingerprints tell the two apart
    lost = (read + latency > visible[-1]) & (changes > 0)
    if np.any(lost):
        covered[int(np.argmax(lost)) :] = False

    return np.where(covered, game, -1)


def _on_frames(seconds: np.ndarray, fps: Fraction) -> np.ndarray:
    """Times in seconds as positions on the video's own frame count, with their average delay taken out."""
    placed = seconds * float(fps)
    index = np.arange(len(placed))
    return placed - np.median(placed - index)


def _placed(game: np.ndarray, presents: timing_log.Presents | None, read: np.ndarray, fps: Fraction) -> np.ndarray:
    """Where each recorded frame's picture belongs, in frames of the video's own timeline."""
    if presents is None:
        return _on_frames(read, fps)

    moment = np.where(game >= 0, presents.simulated[np.maximum(game, 0)], math.nan)
    covered = np.isfinite(moment)

    # a stretch the game's frames don't cover has to go somewhere, and obs's read is the only thing known
    # about it. the two clocks are a fixed distance apart, which is measured where both are known
    apart = np.median(moment[covered] - read[covered]) if np.any(covered) else 0.0
    return _on_frames(np.where(covered, moment, read + apart), fps)


def _pick(times: list[int], n: int) -> tuple[int, int]:
    """Which of `times` a decision for frame n holds: the one at or before it, through the first past it."""
    begin = max(0, bisect.bisect_right(times, n * TIME_SCALE) - 1)
    stop = min(bisect.bisect_left(times, (n + 1) * TIME_SCALE), len(times) - 1)
    return begin, min(max(stop, begin), begin + SLOTS)


def _timeline(
    frames: list[int], placed: list[float], start: int, length: int, hold: int
) -> tuple[list[int], list[int], int]:
    """Real frames and their times relative to `start`, holding the picture across gaps longer than `hold`, and
    how many of them the timing didn't manage to separate."""
    frames_out: list[int] = []
    times_out: list[int] = []
    crowded = 0
    for k, at in zip(frames, placed):
        frame = min(max(k - start, 0), length - 1)
        time = round((at - start) * TIME_SCALE)

        if times_out and time - times_out[-1] > hold * TIME_SCALE:
            frames_out.append(frames_out[-1])
            times_out.append(time - hold * TIME_SCALE)

        # these are pictures something said were different from the one before, so the model putting this one
        # no later than that one is the model being wrong, not the picture not existing. dropping it would
        # throw away the one thing that is measured here, so it goes in at the smallest step the timeline can
        # tell apart and the caller is told how often that happened
        if times_out and time <= times_out[-1]:
            time = times_out[-1] + 1
            crowded += 1

        frames_out.append(frame)
        times_out.append(time)

    return frames_out, times_out, crowded


def _decisions(
    new: np.ndarray, placed: np.ndarray, start: int, length: int, hold: int
) -> tuple[vs.VideoNode, int, int]:
    """A decision per frame of the trimmed part, from the frames the log says are real and when they belong,
    and how many of those frames the timing put no later than the one before."""
    real = [int(k) for k in np.nonzero(new)[0]]

    # the real frames in the trimmed part, and one either side so its ends have something to move towards
    low = bisect.bisect_left(real, start)
    high = bisect.bisect_right(real, start + length - 1)
    kept = real[max(low - 1, 0) : min(high + 1, len(real))]
    frames_list, times_list, crowded = _timeline(kept, [placed[k] for k in kept], start, length, hold)

    gaps = np.diff(times_list) / TIME_SCALE if len(times_list) > 1 else np.array([1.0])
    widest = int(max(2, math.ceil(gaps.max())))

    holder = core.std.BlankClip(width=1, height=1, format=vs.GRAY8, length=length, keep=True)

    def decide(n: int, f: vs.VideoFrame) -> vs.VideoFrame:
        begin, stop = _pick(times_list, n)
        out = f.copy()
        out.props[retime.PROP_FRAMES] = frames_list[begin : stop + 1]
        out.props[retime.PROP_TIMES] = times_list[begin : stop + 1]
        out.props[retime.PROP_TIME_SCALE] = TIME_SCALE
        return out

    return core.std.ModifyFrame(holder, holder, decide), widest, crowded


def analyse(
    fps: Fraction,
    frames: int,
    start: int,
    length: int,
    video_path: Path,
    hold: int,
) -> retime.Timeline | None:
    """Set up retiming from a video's frame timing log, or None if it has none that fits.

    `frames` is the whole video's length and `fps` its framerate before any timescale; what's retimed is the
    `length` frames from `start`. A picture is held rather than interpolated across a gap wider than `hold`
    frames. No frame of the video is read: everything here comes out of the log.
    """
    logs = timing_log.sidecars_for(video_path)
    if not logs:
        return None

    sidecar_path = logs[0]
    try:
        sizes = timing_log.packet_sizes(video_path, frames)
        sidecar_path, sidecar, first = timing_log.find_sidecar(logs, sizes)
        ticks = timing_log.render_ticks(sidecar, sizes, first)
        read, fingerprint, timed_by = timing_log.reads_for(sidecar, ticks)
        clip = slice(start, start + length)
        presents, missing_reason = timing_log.game_presents(sidecar, read[clip][0] - LEAD, read[clip][-1] + LEAD)
    except (timing_log.LogError, OSError, ValueError, KeyError, IndexError) as e:
        log.info(f"frame timing: not using {sidecar_path.name} ({e})")
        return None

    # a recording at a divided framerate is fine - the timeline is built in the video's own frames either way,
    # and a packet is tied to its tick by obs's own timestamp - but anything else isn't this video's log
    if fps <= 0 or sidecar.fps % fps != 0:
        log.info(
            f"frame timing: {sidecar_path.name} was written at {float(sidecar.fps):g}fps, and this video runs "
            f"at {float(fps):g}fps"
        )

    changes = picture_changes(fingerprint)
    known = int(np.count_nonzero(changes[clip] >= 0))
    if known < length:
        log.info(
            f"frame timing: {length - known} of {length} frames have no picture fingerprint, so whether they "
            f"repeat isn't known - is the probe filter's fingerprinting off?"
        )

    game = np.zeros(len(read), dtype=np.int64)
    if presents is not None:
        # fitting the two free numbers needs a stretch of the clip, not all of it - but it has to start where
        # the run of presents the fit is used on starts, or the phase means something different
        sample = slice(start, start + min(length, FIT_FRAMES))
        near = presents.present <= read[sample][-1] + LEAD

        if np.any(changes[sample] >= 0) and np.count_nonzero(near) > 1:
            phase, latency, wrong, identified = fit(
                presents.present[near],
                presents.visible[near],
                read[sample],
                changes[sample],
                presents.interval,
                fps,
            )
            if identified:
                fitted = (
                    f"read {latency * 1e3:+.2f}ms after the probe, "
                    f"{wrong / max(int(np.count_nonzero(changes[sample] > 0)), 1):.1%} of picture changes "
                    f"unexplained"
                )
            else:
                fitted = (
                    "the fingerprints can't say when the read happened - the game outruns the recording, so "
                    "nearly every read sees a new picture whenever it happened - and the probe's measurement "
                    "stands"
                )
        else:
            phase, latency = 1.0, 0.0
            fitted = "nothing to fit it to, so the read is taken as the probe measured it"

        game = game_frames(read, presents, changes, phase, latency)
        covered = game[clip] >= 0
        log.info(
            f"frame timing: {presents.process}'s frames from {sidecar_path.name}, timed by "
            f"{presents.simulated_by}, {covered.mean():.0%} of frames covered, {fitted}"
        )
        log.info(f"frame timing: obs's reads timed by {timed_by}")
    else:
        log.info(
            f"frame timing: obs's reads from {sidecar_path.name}, without the game's frames ({missing_reason}) - "
            f"timing is less accurate when the game runs faster than the recording"
        )

    # the fingerprints say which frames are new pictures; where a frame hasn't got one, the game's frames are
    # the next best thing, and failing that every frame is taken as its own picture
    shows = np.concatenate([[True], game[1:] != game[:-1]]) if presents is not None else np.ones(len(read), bool)
    new = np.where(changes >= 0, changes > 0, shows)

    placed = _placed(game, presents, read, fps)
    decisions, widest, crowded = _decisions(new, placed, start, length, hold)
    if crowded:
        log.info(
            f"frame timing: {crowded} of {int(np.count_nonzero(new[clip]))} pictures were timed no later than "
            f"the one before them, so the timing disagrees with the fingerprints there and they're only just "
            f"kept apart"
        )

    return retime.Timeline(
        decisions=decisions,
        length=length,
        max_gap=widest,
        resolution=1,
        slots=SLOTS,
    )
