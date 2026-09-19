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


def _latency(read: np.ndarray, changes: np.ndarray, visible: np.ndarray, reach: float) -> tuple[int, float, float]:
    """The read latency that contradicts the fingerprints least: how many changes it can't account for, the
    latency itself, and how wide the range of latencies that score the same is.

    A recorded frame shows the newest copy that had become visible by the time obs read it, give or take a
    fixed offset. There is one: the probe's flush finishing isn't quite when the draw sampled the shared
    texture, and a game frame's last GPU work isn't quite when the hook's copy of it finished. The
    fingerprints say exactly which recorded frames are new pictures, so the offset is chosen to disagree with
    them as rarely as possible.

    Every offset that would change which copy a frame saw is tried - those are the differences between a
    copy's time and a read - and of the offsets that disagree least, the middle of the widest run of them
    wins, which is the one furthest from being a close call.
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

    # only one of the two ways the model and the fingerprints can differ is impossible: a frame whose picture
    # changed cannot be showing the same copy as the frame before it. the other way round happens for real -
    # the game can draw two frames that look identical, and often does when nothing is moving - so counting
    # that as an error would fit the timing to how still the game was
    def disagrees(i: int) -> int:
        if i <= 0 or i >= frames or changes[i] <= 0 or shown[i] < 0 or shown[i - 1] < 0:
            return 0
        return int(shown[i] == shown[i - 1])

    wrong = sum(disagrees(i) for i in range(frames))

    best = (wrong, -reach, reach)
    step = 0
    while step < len(offset):
        latency = offset[step]
        while step < len(offset) and offset[step] == latency:
            frame = int(of[step])
            wrong -= disagrees(frame) + disagrees(frame + 1)
            shown[frame] = copy[step]
            wrong += disagrees(frame) + disagrees(frame + 1)
            step += 1

        until = offset[step] if step < len(offset) else reach
        if wrong < best[0] or (wrong == best[0] and until - latency > best[2] - best[1]):
            best = (wrong, latency, until)

    return best[0], float((best[1] + best[2]) / 2), float(best[2] - best[1])


def fit(
    present: np.ndarray, visible: np.ndarray, read: np.ndarray, changes: np.ndarray, interval: float, fps: Fraction
) -> tuple[float, float, int]:
    """The hook's grid phase and the read latency that fit the fingerprints best.

    The phase only matters when the hook skips presents at all - a game presenting slower than the interval
    has every frame copied whatever the grid is doing - so it's only looked for when it can make a difference.
    """
    reach = LATENCY_REACH / float(fps)
    skips = len(present) > 1 and float(np.min(np.diff(present))) < interval
    phases = np.linspace(1, 0, PHASE_STEPS, endpoint=False) if skips else np.array([1.0])

    best: tuple[int, float, float, float] | None = None
    for phase in phases:
        copied = hook_copies(present, interval, float(phase))
        if len(copied) == 0:
            continue

        wrong, latency, width = _latency(read, changes, np.sort(visible[copied]), reach)
        if best is None or (wrong, -width) < (best[0], -best[3]):
            best = (wrong, latency, float(phase), width)

    # nothing was copied under any phase, so there's nothing to fit and nothing to fit it to
    return (best[2], best[1], best[0]) if best else (1.0, 0.0, 0)


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
) -> tuple[list[int], list[int]]:
    """Real frames and their times relative to `start`, holding the picture across gaps longer than `hold`."""
    frames_out: list[int] = []
    times_out: list[int] = []
    for k, at in zip(frames, placed):
        frame = min(max(k - start, 0), length - 1)
        time = round((at - start) * TIME_SCALE)

        if times_out and time - times_out[-1] > hold * TIME_SCALE:
            frames_out.append(frames_out[-1])
            times_out.append(time - hold * TIME_SCALE)

        if times_out and time <= times_out[-1]:
            continue
        frames_out.append(frame)
        times_out.append(time)

    return frames_out, times_out


def _decisions(
    new: np.ndarray, placed: np.ndarray, start: int, length: int, hold: int
) -> tuple[vs.VideoNode, int]:
    """A decision per frame of the trimmed part, from the frames the log says are real and when they belong."""
    real = []
    last_time = -math.inf
    for k in np.nonzero(new)[0]:
        if placed[k] > last_time:
            real.append(int(k))
            last_time = placed[k]

    # the real frames in the trimmed part, and one either side so its ends have something to move towards
    low = bisect.bisect_left(real, start)
    high = bisect.bisect_right(real, start + length - 1)
    kept = real[max(low - 1, 0) : min(high + 1, len(real))]
    frames_list, times_list = _timeline(kept, [placed[k] for k in kept], start, length, hold)

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

    return core.std.ModifyFrame(holder, holder, decide), widest


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
    sidecar_path = timing_log.sidecar_path(video_path)
    if not sidecar_path.exists():
        return None

    try:
        sidecar = timing_log.load_sidecar(sidecar_path)
        sizes = timing_log.packet_sizes(video_path, frames)
        ticks = timing_log.render_ticks(sidecar, sizes)
        read, fingerprint = timing_log.reads_for(sidecar, ticks)
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
            phase, latency, wrong = fit(
                presents.present[near],
                presents.visible[near],
                read[sample],
                changes[sample],
                presents.interval,
                fps,
            )
            fitted = (
                f"read {latency * 1e3:+.2f}ms after the probe, "
                f"{wrong / max(int(np.count_nonzero(changes[sample] > 0)), 1):.1%} of picture changes unexplained"
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
    decisions, widest = _decisions(new, placed, start, length, hold)

    return retime.Timeline(
        decisions=decisions,
        length=length,
        max_gap=widest,
        resolution=1,
        slots=SLOTS,
    )
