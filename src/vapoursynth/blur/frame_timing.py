"""Frame timing - putting frames back where the game really drew them.

A recording's frames aren't evenly spaced in game time. Each recorded frame shows whichever game frame the
recorder last got hold of: recording a 500fps game at 360fps, sometimes one game frame has gone by since the
previous recorded frame and sometimes two, and a game that stutters or a recorder that falls behind makes it
worse. Steady motion comes out uneven, and interpolating as though the frames were evenly spaced keeps it
that way.

None of that can be read reliably from the picture, so it's read from a log instead - see
blur/frame_timing_log.py for the file itself. It says when OBS's GPU really read the captured picture for each
recorded frame, and - when it had permission to trace them - when the game made each of its frames. From
those, every recorded frame is tied to the game frame it shows:

- with a game capture, OBS's hook copies the game's frames as they're presented, skipping any that come
  sooner than half a recorded frame after its last copy, and a copy becomes readable once the GPU has
  finished the game frame it was queued behind
- with a window capture, the compositor hands OBS the window instead: nothing is skipped, a frame becomes
  readable when it reaches the screen, and one that never got there was never captured
- either way the recorded frame shows the last frame readable when OBS read it

and each game frame is placed when the game simulated it. Games that report that - through NVIDIA Reflex or
Intel's PresentMon markers - say exactly; for the rest it's taken as the moment the game started the frame,
the same fallback PresentMon uses. That places every recorded frame on the game's timeline, which becomes the
timeline interpolation renders - see blur/retime.py. A recorded frame showing the same game frame as the one
before is a repeat and drops out, so this takes deduplication's place rather than running alongside it.

Without the game's frames, each new picture is placed at the moment OBS read it, and repeats are found the way
deduplication finds them. That handles a recorder that falls behind, but not a game running faster than the
recording.
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

# the frame that obs rendered on a tick carries the previous tick's timestamp
RENDER_DELAY = 1

# how long after the probe saw obs's read finish the picture really counts as read
READ_LATENCY = 0.00015

# a frame's position is kept to this fraction of a frame
TIME_SCALE = 1 << 16

# a decision spans one frame of the timeline, which can hold a couple of real frames landing partway through it
SLOTS = 3

# gaps between real frames longer than this, in frames, hold the picture and move over the last this many
MAX_GAP = retime.MAX_GAP_LIMIT

# how far either side of itself a frame looks for repeats when the log has no game frames
REPEAT_REACH = 8


def hook_copies(present: np.ndarray, interval: float) -> np.ndarray:
    """Which presents the capture hook copied. It skips one that comes sooner than `interval` after its last
    copy, counting the interval on a fixed grid, the way OBS's frame_ready does."""
    copied = []
    last = -math.inf
    for i, t in enumerate(present):
        elapsed = t - last
        if elapsed < interval:
            continue
        last = t if elapsed > interval * 2 else last + interval
        copied.append(i)
    return np.array(copied, dtype=np.int64)


def read_times(sidecar: timing_log.Sidecar, sizes: np.ndarray) -> np.ndarray:
    """When OBS's GPU read the captured picture for each of the video's frames, in seconds from the save."""
    frames = len(sizes)
    first = timing_log.match_packets(sidecar, sizes)
    frequency, saved = sidecar.qpc_frequency, sidecar.saved_qpc

    packets = sidecar.packets[first : first + frames]
    packets = packets[np.argsort(packets["pts"], kind="stable")]

    # a stamp lands a few microseconds either side of its tick's time. encoders count pts in frames, so a frame
    # decoded before it's shown is stamped that many frames later than its decode time
    frame_us = 1_000_000 * sidecar.fps.denominator / sidecar.fps.numerator
    stamp_us = packets["sys_dts_usec"] + np.round((packets["pts"] - packets["dts"]) * frame_us).astype(np.int64)
    tick_us = sidecar.ticks["frame_time"].astype(np.int64) // 1000
    after = np.clip(np.searchsorted(tick_us, stamp_us), 1, len(tick_us) - 1)
    nearest = np.where(tick_us[after] - stamp_us < stamp_us - tick_us[after - 1], after, after - 1)
    rendered = np.clip(nearest + RENDER_DELAY, 0, len(tick_us) - 1)

    saved_ns = saved * 1_000_000_000 // frequency
    tick_time = (sidecar.ticks["frame_time"].astype(np.int64)[rendered] - saved_ns) / 1e9

    reads = sidecar.reads
    reported = reads["done_qpc"] > 0
    read_at = {
        int(t): (int(q) - saved) / frequency
        for t, q in zip(reads["frame_time"][reported], reads["done_qpc"][reported])
    }
    read = np.array([read_at.get(int(t), math.nan) for t in sidecar.ticks["frame_time"][rendered]])

    # a read the probe didn't report is taken to have run as late as reads usually do
    missing = np.isnan(read)
    if missing.all():
        raise timing_log.LogError("the log has no reads - is the Frame Timing Probe filter on the game capture?")
    if missing.any():
        read[missing] = tick_time[missing] + np.nanmedian(read - tick_time)

    return read


def game_frames(read: np.ndarray, presents: timing_log.Presents, fps: Fraction) -> np.ndarray:
    """For each read, the game frame it saw. Reads the game's frames don't cover get -1."""
    if presents.hooked:
        interval = math.floor(fps.denominator * 1e9 / fps.numerator) / 1e9 / 2
        copied = hook_copies(presents.present, interval)
    else:
        # nothing skips frames on the way to a window capture; the compositor's dropped ones are already out
        copied = np.arange(len(presents.present))
    visible = presents.visible[copied]
    order = np.argsort(visible, kind="stable")
    visible = visible[order]
    copied = copied[order]

    index = np.searchsorted(visible, read + READ_LATENCY, "right") - 1
    covered = index >= 0
    game = np.maximum.accumulate(np.where(covered, copied[np.maximum(index, 0)], -1))
    # a read past the game's last frame only saw it if it came right after it
    covered &= read <= presents.present[-1] + 0.1
    return np.where(covered, game, -1)


def moments(game: np.ndarray, presents: timing_log.Presents) -> np.ndarray:
    """When each shown game frame was simulated, in seconds."""
    return np.where(game >= 0, presents.simulated[np.maximum(game, 0)], math.nan)


def _on_frames(seconds: np.ndarray, fps: Fraction) -> np.ndarray:
    """Times in seconds as positions on the video's own frame count, with their average delay taken out."""
    placed = seconds * float(fps)
    known = np.isfinite(placed)
    index = np.arange(len(placed))
    placed = placed - np.nanmedian(placed[known] - index[known])
    return np.where(known, placed, index.astype(float))


def _pick(times: list[int], n: int) -> tuple[int, int]:
    """Which of `times` a decision for frame n holds: the one at or before it, through the first past it."""
    begin = max(0, bisect.bisect_right(times, n * TIME_SCALE) - 1)
    stop = min(bisect.bisect_left(times, (n + 1) * TIME_SCALE), len(times) - 1)
    return begin, min(max(stop, begin), begin + SLOTS)


def _held_timeline(frames: list[int], placed: list[float], start: int, length: int) -> tuple[list[int], list[int]]:
    """Real frames and their times relative to `start`, holding the picture across gaps longer than MAX_GAP."""
    frames_out: list[int] = []
    times_out: list[int] = []
    for k, at in zip(frames, placed):
        frame = min(max(k - start, 0), length - 1)
        time = round((at - start) * TIME_SCALE)

        if times_out and time - times_out[-1] > MAX_GAP * TIME_SCALE:
            frames_out.append(frames_out[-1])
            times_out.append(time - MAX_GAP * TIME_SCALE)

        if times_out and time <= times_out[-1]:
            continue
        frames_out.append(frame)
        times_out.append(time)

    return frames_out, times_out


def _logged_decisions(game: np.ndarray, placed: np.ndarray, start: int, length: int) -> tuple[vs.VideoNode, int]:
    """Decisions from a timeline that already says which frames are repeats."""
    index = np.arange(len(game))
    game = np.where(game >= 0, game, -1 - index)
    new = np.concatenate([[True], game[1:] != game[:-1]])

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
    frames_list, times_list = _held_timeline(kept, [placed[k] for k in kept], start, length)

    gaps = np.diff(times_list) / TIME_SCALE if len(times_list) > 1 else np.array([1.0])
    max_gap = int(min(MAX_GAP, max(2, math.ceil(np.percentile(gaps, 99)))))

    holder = core.std.BlankClip(width=1, height=1, format=vs.GRAY8, length=length, keep=True)

    def decide(n: int, f: vs.VideoFrame) -> vs.VideoFrame:
        begin, stop = _pick(times_list, n)
        out = f.copy()
        out.props[retime.PROP_FRAMES] = frames_list[begin : stop + 1]
        out.props[retime.PROP_TIMES] = times_list[begin : stop + 1]
        out.props[retime.PROP_TIME_SCALE] = TIME_SCALE
        return out

    return core.std.ModifyFrame(holder, holder, decide), max_gap


def _read_decisions(
    video: vs.VideoNode, placed: np.ndarray, start: int, threshold: float
) -> tuple[vs.VideoNode, int]:
    """Decisions from read times alone, with repeats found by how much the picture changed.

    `video` is the trimmed part. Each decision only reads the frames within REPEAT_REACH of itself.
    """
    length = video.num_frames
    diffs = core.std.PlaneStats(video, video[0] + video)
    offsets = list(range(-REPEAT_REACH, REPEAT_REACH + 1))
    window = [retime.shifted(diffs, offset) for offset in offsets]
    holder = core.std.BlankClip(width=1, height=1, format=vs.GRAY8, length=length, keep=True)
    base = 1 + offsets.index(0)

    def decide(n: int, f: list[vs.VideoFrame]) -> vs.VideoFrame:
        low = max(0, n - REPEAT_REACH)
        high = min(length - 1, n + REPEAT_REACH)

        # a frame is real if it differs from the one before. whatever the window starts on stands in for the
        # real frame before it, if that's out of reach
        real = [k for k in range(low, high + 1) if k == low or f[base + k - n].props["PlaneStatsDiff"] >= threshold]
        frames_list, times_list = _held_timeline(
            [start + k for k in real], [placed[start + k] for k in real], start, length
        )

        begin, stop = _pick(times_list, n)
        out = f[0].copy()
        out.props[retime.PROP_FRAMES] = frames_list[begin : stop + 1]
        out.props[retime.PROP_TIMES] = times_list[begin : stop + 1]
        out.props[retime.PROP_TIME_SCALE] = TIME_SCALE
        return out

    return core.std.ModifyFrame(holder, [holder, *window], decide), REPEAT_REACH


def analyse(
    clip: vs.VideoNode,
    fps: Fraction,
    start: int,
    length: int,
    video_path: Path,
    repeat_threshold: float,
) -> retime.Timeline | None:
    """Set up retiming from a video's frame timing log, or None if it has none that fits.

    `clip` is the whole video, `fps` its framerate before any timescale, and what's retimed is the `length`
    frames of it from `start`. `repeat_threshold` is deduplication's threshold, used to find repeats when the
    log doesn't have the game's frames.
    """
    sidecar_path = timing_log.sidecar_path(video_path)
    if not sidecar_path.exists():
        return None

    try:
        sidecar = timing_log.load_sidecar(sidecar_path)
        sizes = timing_log.packet_sizes(video_path, clip.num_frames)
        read = read_times(sidecar, sizes)
        presents, missing_reason = timing_log.game_presents(sidecar)
    except (timing_log.LogError, OSError, ValueError, KeyError) as e:
        log.info(f"frame timing: not using {sidecar_path.name} ({e})")
        return None

    if presents is not None:
        game = game_frames(read, presents, fps)
        covered = game[start : start + length] >= 0
        log.info(
            f"frame timing: {presents.process}'s frames from {sidecar_path.name}, timed by "
            f"{presents.simulated_by}, {covered.mean():.0%} of frames covered, "
            f"{int((np.diff(game[start : start + length]) == 0).sum())} repeats"
        )
        decisions, max_gap = _logged_decisions(
            game, _on_frames(moments(game, presents), fps), start, length
        )
    else:
        log.info(
            f"frame timing: OBS's reads from {sidecar_path.name}, without the game's frames ({missing_reason}) - "
            f"timing is less accurate when the game runs faster than the recording"
        )
        decisions, max_gap = _read_decisions(
            clip[start : start + length], _on_frames(read, fps), start, repeat_threshold
        )

    return retime.Timeline(
        decisions=decisions,
        length=length,
        max_gap=max_gap,
        resolution=1,
        slots=SLOTS,
    )
