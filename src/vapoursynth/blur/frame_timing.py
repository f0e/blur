"""Frame timing - putting frames back where the game really drew them.

A recording's frames aren't evenly spaced in game time, so steady motion comes out uneven. The frame timing log
(see blur/frame_timing_log.py) says which recorded frames are new pictures (the probe hashes what obs read on
each tick) and, when the plugin could trace the game, when each game frame was simulated and became readable.

The hook's skip grid phase and the read latency aren't in the log, so they're fitted to the fingerprints. The
result is a timeline for blur/retime.py, which replaces deduplication. Without the game's frames each new
picture is placed at the moment obs read it.
"""

import bisect
import math
from fractions import Fraction
from pathlib import Path

import numpy as np

import blur.frame_timing_log as timing_log
import vapoursynth as vs
from blur import log, retime
from vapoursynth import core

# a frame's position is kept to this fraction of a frame
TIME_SCALE = 1 << 16

# a decision spans one frame of the timeline, which can hold a couple of real frames landing partway through it
SLOTS = 3

# read latency search range either side of zero, in recorded frames
LATENCY_REACH = 1.0

# hook skip grid phases tried
PHASE_STEPS = 16

# if more of the latency range than this fits equally well, the fit isn't used. clips the fingerprints pin down
# leave under a percent, ones they can't leave a third or more
IDENTIFIABLE_SHARE = 0.1

# frames the phase and latency are fitted over
FIT_FRAMES = 20000

# seconds of game frames wanted either side of the clip
LEAD = 2.0


def hook_copies(present: np.ndarray, interval: float, phase: float) -> np.ndarray:
    """Which presents the capture hook copied, skipping ones sooner than `interval` on a fixed grid like obs's
    `frame_ready`. `phase` is how far before the first present the grid last fell, as a share of an interval"""
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
    """The read latency that disagrees with the fingerprints least. Returns the number of disagreements, the
    latency (middle of the widest best-scoring run), that run's width, and the total width scoring the same"""
    frames = len(read)
    low = np.searchsorted(visible, read - reach, "right")
    high = np.searchsorted(visible, read + reach, "right")

    # every (frame, copy) pair the latency could flip, sorted by latency
    of = np.repeat(np.arange(frames), high - low)
    copy = np.concatenate([np.arange(a, b) for a, b in zip(low, high)]) if frames else np.empty(0, np.int64)
    offset = visible[copy] - read[of]
    order = np.argsort(offset, kind="stable")
    of, copy, offset = of[order], copy[order], offset[order]

    shown = low.astype(np.int64) - 1

    # frames past the newest copy aren't scored, using the whole reach so it doesn't depend on the latency
    scorable = (read + reach <= visible[-1]) if len(visible) else np.zeros(frames, dtype=bool)

    # only a changed picture showing the same copy is wrong. games can draw identical frames, so the reverse isn't
    def disagrees(i: int) -> int:
        if i <= 0 or i >= frames or changes[i] <= 0 or not scorable[i] or shown[i] < 0 or shown[i - 1] < 0:
            return 0
        return int(shown[i] == shown[i - 1])

    wrong = sum(disagrees(i) for i in range(frames))

    # the score only changes at those pairs, so sweep them as intervals
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

    # merge neighbouring intervals with the same score
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
    identifiable. The phase is only searched when the hook actually skips presents"""
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

    if best is None:
        return 1.0, 0.0, 0, False

    # a game outrunning the recording shows a new picture on nearly every read, so any latency fits and the
    # probe's measurement is kept instead
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

    # past the last logged game frame, a picture change means the log ran out rather than the game stopping
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

    # uncovered frames fall back to obs's read, offset by the median distance between the two where both are known
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

        # the fingerprints say this is a new picture, so keep it at the smallest possible step rather than drop it
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

    # the real frames in the trimmed part plus one either side
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
    """Set up retiming from a video's frame timing log, or None if it has none that fits. `frames` and `fps` are
    the whole video's, before timescale. Gaps wider than `hold` frames are held rather than interpolated"""
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

    log.status("frame-timing-log", sidecar_path.name)

    # a divided framerate is fine, anything else means this isn't the video's log
    if fps <= 0 or sidecar.fps % fps != 0:
        log.info(
            f"frame timing: {sidecar_path.name} was written at {float(sidecar.fps):g}fps, and this video runs "
            f"at {float(fps):g}fps"
        )

    changes = picture_changes(fingerprint)
    known = int(np.count_nonzero(changes[clip] >= 0))
    if known < length:
        log.info(
            f"frame timing: {length - known} of {length} frames have no fingerprint (is the probe filter's "
            f"fingerprinting off?)"
        )

    game = np.zeros(len(read), dtype=np.int64)
    if presents is not None:
        # fit over the start of the clip, the phase depends on where the run of presents starts
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
                fitted = "couldn't fit the read latency (game faster than the recording), using the probe's timing"
        else:
            phase, latency = 1.0, 0.0
            fitted = "nothing to fit, using the probe's timing"

        game = game_frames(read, presents, changes, phase, latency)
        covered = game[clip] >= 0
        log.info(
            f"frame timing: {presents.process}'s frames from {sidecar_path.name}, timed by "
            f"{presents.simulated_by}, {covered.mean():.0%} of frames covered, {fitted}"
        )
        log.info(f"frame timing: obs's reads timed by {timed_by}")
    else:
        log.info(
            f"frame timing: obs's reads from {sidecar_path.name}, without the game's frames ({missing_reason}). "
            f"less accurate when the game runs faster than the recording"
        )

    # fingerprints first, then the game's frames, then every frame is new
    shows = np.concatenate([[True], game[1:] != game[:-1]]) if presents is not None else np.ones(len(read), bool)
    new = np.where(changes >= 0, changes > 0, shows)

    placed = _placed(game, presents, read, fps)
    decisions, widest, crowded = _decisions(new, placed, start, length, hold)
    if crowded:
        log.info(
            f"frame timing: {crowded} of {int(np.count_nonzero(new[clip]))} frames were timed no later than the "
            f"one before them"
        )

    return retime.Timeline(
        decisions=decisions,
        length=length,
        max_gap=widest,
        resolution=1,
        slots=SLOTS,
    )
