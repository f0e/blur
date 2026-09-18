"""Frame timing - putting frames back where the game really drew them.

A recording's frames aren't evenly spaced in game time. Each recorded frame shows whichever game frame the
recorder last got hold of: recording a 500fps game at 360fps, sometimes one game frame has gone by since the
previous recorded frame and sometimes two, and a game that stutters or a recorder that falls behind makes it
worse. Steady motion comes out uneven, and interpolating as though the frames were evenly spaced keeps it
that way.

None of that can be read reliably from the picture, so it's read from a log instead: the
obs-frame-timing-recorder OBS plugin saves a `.frametiming` file next to each recording and replay, saying
when OBS's GPU really read the captured picture for each recorded frame, and - when it has permission to
trace them - when the game made each of its frames. From those, every recorded frame is tied to the game
frame it shows:

- with a game capture, OBS's hook copies the game's frames as they're presented, skipping any that come
  sooner than half a recorded frame after its last copy, and a copy becomes readable once the GPU has
  finished the game frame it was queued behind
- with a window capture, the compositor hands OBS the window instead: nothing is skipped, a frame becomes
  readable when it reaches the screen, and one that never got there was never captured
- either way the recorded frame shows the last frame readable when OBS read it

and each game frame is placed when the game simulated it. Games that report that - through NVIDIA Reflex or
Intel's PresentMon markers - say exactly; for the rest it's taken as the moment the game started the frame,
the same fallback PresentMon uses. That places every recorded frame on the game's timeline, which is handed
to interpolation the same way deduplication hands over its own - see blur/deduplicate.py. A recorded frame
showing the same game frame as the one before is a repeat and drops out, so this takes deduplication's
place.

Without the game's frames, each new picture is placed at the moment OBS read it, and repeats are found the way
deduplication finds them. That handles a recorder that falls behind, but not a game running faster than the
recording.

Recorded frames are matched to the plugin's log by the sizes of their encoded packets, which the log has too.
"""

import vapoursynth as vs
from vapoursynth import core

import bisect
import math
import shutil
import struct
import subprocess
from collections import Counter
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path

import numpy as np

import blur.deduplicate as deduplicate
from blur import log

SIDECAR_SUFFIX = ".frametiming"
SIDECAR_VERSION = 6

# the frame that obs rendered on a tick carries the previous tick's timestamp
RENDER_DELAY = 1

# how long after the probe saw obs's read finish the picture really counts as read
READ_LATENCY = 0.00015

# the share of a replay's packets whose size has to differ from the log's by the same amount to match it
MATCH_SHARE = 0.98

# a frame's position is kept to this fraction of a frame
TIME_SCALE = 1 << 16

# a decision spans one frame of the timeline, which can hold a couple of real frames landing partway through it
SLOTS = 3

# gaps between real frames longer than this, in frames, hold the picture and move over the last this many
MAX_GAP = deduplicate.MAX_GAP_LIMIT

# how far either side of itself a frame looks for repeats when the log has no game frames
REPEAT_REACH = 8

HEADER = struct.Struct("<8sIIqqII")
BATCH = struct.Struct("<4sI")

TICK = np.dtype([("qpc", "<i8"), ("frame_time", "<u8"), ("total_frames", "<u8"), ("lagged_frames", "<u8")])
READ = np.dtype([("frame_time", "<u8"), ("submitted_qpc", "<i8"), ("done_qpc", "<i8")])
PACKET = np.dtype([
    ("pts", "<i8"),
    ("dts", "<i8"),
    ("dts_usec", "<i8"),
    ("sys_dts_usec", "<i8"),
    ("size", "<u8"),
    ("keyframe", "<u8"),
    ("cts", "<u8"),
    ("fer", "<u8"),
    ("ferc", "<u8"),
    ("received_qpc", "<i8"),
])
PRESENT = np.dtype([
    ("present_start", "<u8"),
    ("time_in_present", "<u8"),
    ("gpu_start", "<u8"),
    ("ready", "<u8"),
    ("gpu_duration", "<u8"),
    ("swap_chain", "<u8"),
    ("process_id", "<u8"),
    ("runtime", "<u8"),
    ("present_mode", "<u8"),
    ("final_state", "<u8"),
    ("flags", "<u8"),
    ("app_sim_start", "<u8"),
    ("reflex_sim_start", "<u8"),
    ("screen_time", "<u8"),
    ("window", "<u8"),
])
GAME = np.dtype([("process_id", "<u8"), ("capture", "<u8"), ("window", "<u8"), ("name", "S120")])

# how obs was capturing, which says when a game frame became something it could read
HOOK_CAPTURE = 0
WINDOW_CAPTURE = 1

PRESENT_FAILED = 1 << 1

# why a log has no game frames, from the plugin's point of view
GAME_TIMING = {1: "OBS had no permission to trace them", 2: "tracing them failed", 3: "tracing never started"}


class LogError(Exception):
    pass


@dataclass
class Sidecar:
    qpc_frequency: int
    saved_qpc: int
    fps: Fraction
    ticks: np.ndarray
    reads: np.ndarray
    packets: np.ndarray
    presents: np.ndarray
    games: np.ndarray
    game_timing: int


@dataclass
class Presents:
    process: str
    start: np.ndarray
    present: np.ndarray
    gpu_start: np.ndarray
    gpu_done: np.ndarray
    # when obs could first read each frame, and whether its capture saw every one of them
    visible: np.ndarray
    hooked: bool
    # when each frame was simulated, and what said so
    simulated: np.ndarray
    simulated_by: str


def load_sidecar(path: Path) -> Sidecar:
    data = path.read_bytes()
    magic, version, game_timing, frequency, saved, num, den = HEADER.unpack_from(data)
    if magic != b"BLURFTIM":
        raise LogError(f"{path.name} isn't a frame timing log")
    if version != SIDECAR_VERSION:
        raise LogError(f"{path.name} is a version {version} log, and this version of blur reads {SIDECAR_VERSION}")

    # the header is followed by batches of records, one tag at a time. a recording appends more as it runs, so
    # a tag turns up once per batch written and the last batch can be short if obs died mid-recording
    dtypes = {b"TICK": TICK, b"READ": READ, b"PCKT": PACKET, b"PRES": PRESENT, b"GAME": GAME}
    batches: dict[bytes, list[np.ndarray]] = {tag: [] for tag in dtypes}
    offset = HEADER.size
    while offset + BATCH.size <= len(data):
        tag, count = BATCH.unpack_from(data, offset)
        offset += BATCH.size
        dtype = dtypes.get(tag)
        if dtype is None:
            raise LogError(f"{path.name} has a {tag!r} batch, which this version of blur doesn't know")
        count = min(count, (len(data) - offset) // dtype.itemsize)
        batches[tag].append(np.frombuffer(data, dtype, count, offset))
        offset += count * dtype.itemsize

    sections = {tag: np.concatenate(parts) if parts else np.empty(0, dtypes[tag]) for tag, parts in batches.items()}
    if len(sections[b"TICK"]) == 0 or len(sections[b"PCKT"]) == 0:
        raise LogError(f"{path.name} has no ticks or no packets")

    # the plugin logs only what obs captured, and says here what it was and how

    return Sidecar(
        frequency,
        saved,
        Fraction(num, den),
        sections[b"TICK"],
        sections[b"READ"],
        sections[b"PCKT"],
        sections[b"PRES"],
        sections[b"GAME"],
        game_timing,
    )


def sidecar_presents(sidecar: Sidecar) -> tuple[Presents | None, str]:
    """The game's frames from the plugin's own trace, in seconds from when the sidecar was saved, or why
    there aren't any.

    The log holds the captured game's presents and nothing else; its busiest swapchain is the captured one.
    Game capture hooks the game and gets its frames as it presents them, so a frame is readable once the GPU
    has finished it. Window capture is handed the window by the compositor instead, so a frame is readable
    when it reaches the screen, and one that never got there was never captured.
    """
    records = sidecar.presents
    if len(records) == 0:
        return None, GAME_TIMING.get(sidecar.game_timing, "the log doesn't have them")
    records = records[(records["flags"] & PRESENT_FAILED) == 0]

    # a game that restarted mid-log presents under a new id, and the one that presented most is the one shown
    process = Counter(int(p) for p in records["process_id"]).most_common(1)[0][0]
    records = records[records["process_id"] == process]
    captured = sidecar.games[sidecar.games["process_id"] == process]
    hooked = len(captured) == 0 or int(captured[0]["capture"]) == HOOK_CAPTURE

    # a window capture only sees the window it was pointed at
    window = int(captured[0]["window"]) if len(captured) else 0
    if not hooked and window and np.any(records["window"] == window):
        records = records[records["window"] == window]

    chain = Counter(int(c) for c in records["swap_chain"]).most_common(1)[0][0]
    records = records[records["swap_chain"] == chain]
    records = records[np.argsort(records["present_start"], kind="stable")]

    if hooked:
        # placing frames at their presents instead of at gpu done is worse than not placing them at all
        if np.mean(records["ready"] > 0) < 0.5:
            return None, "the log has no gpu timing for them"
    else:
        shown = records["screen_time"] > 0
        if np.mean(shown) < 0.5:
            return None, "the log doesn't say when its frames reached the screen"
        # a frame the compositor dropped never reached the capture
        records = records[shown]

    frequency, saved = sidecar.qpc_frequency, sidecar.saved_qpc
    present_qpc = records["present_start"].astype(np.int64)
    ready_qpc = records["ready"].astype(np.int64)
    gpu_start_qpc = records["gpu_start"].astype(np.int64)

    # a frame starts when the game comes back from presenting the one before, as presentmon has it
    ended = present_qpc + records["time_in_present"].astype(np.int64)
    start_qpc = np.concatenate([[present_qpc[0]], ended[:-1]])

    # the odd frame without gpu timing is taken to have finished when it was presented
    done_qpc = np.where(ready_qpc > 0, ready_qpc, present_qpc)
    gpu_start_qpc = np.where(gpu_start_qpc > 0, gpu_start_qpc, present_qpc)

    # the game's own word on when it simulated each frame beats its start, as presentmon has it. a game reports
    # either for all its frames or none of them, so the one reported for most frames is used throughout
    simulated_qpc = start_qpc
    simulated_by = "frame start"
    for field, by in (("app_sim_start", "PresentMon markers"), ("reflex_sim_start", "NVIDIA Reflex")):
        reported = records[field].astype(np.int64)
        if np.mean(reported > 0) >= 0.5:
            simulated_qpc = np.where(reported > 0, reported, start_qpc)
            simulated_by = by
            break

    visible_qpc = done_qpc if hooked else records["screen_time"].astype(np.int64)

    name = captured[0]["name"].decode(errors="replace") if len(captured) else f"process {process}"
    presents = Presents(
        name,
        ((start_qpc - saved) / frequency)[1:],
        ((present_qpc - saved) / frequency)[1:],
        ((gpu_start_qpc - saved) / frequency)[1:],
        ((done_qpc - saved) / frequency)[1:],
        ((visible_qpc - saved) / frequency)[1:],
        hooked,
        ((simulated_qpc - saved) / frequency)[1:],
        simulated_by,
    )
    return presents, ""


def _mp4_sample_sizes(path: Path) -> np.ndarray | None:
    """The video track's sample sizes from an mp4's sample table, in decode order."""
    containers = {b"moov", b"trak", b"mdia", b"minf", b"stbl"}

    def boxes(f, end):
        while f.tell() + 8 <= end:
            start = f.tell()
            size, kind = struct.unpack(">I4s", f.read(8))
            if size == 1:
                size = struct.unpack(">Q", f.read(8))[0]
            elif size == 0:
                size = end - start
            if size < 8:
                return
            yield kind, f.tell(), start + size
            f.seek(start + size)

    def walk(f, end, track):
        for kind, body, box_end in boxes(f, end):
            if kind == b"trak":
                found = {}
                walk(f, box_end, found)
                if found.get("handler") == b"vide" and "sizes" in found:
                    return found["sizes"]
            elif kind in containers:
                result = walk(f, box_end, track)
                if result is not None:
                    return result
            elif kind == b"hdlr":
                f.seek(body + 8)
                track["handler"] = f.read(4)
            elif kind == b"stsz":
                f.seek(body + 4)
                fixed, count = struct.unpack(">II", f.read(8))
                if fixed:
                    track["sizes"] = np.full(count, fixed, dtype=np.int64)
                else:
                    track["sizes"] = np.frombuffer(f.read(4 * count), dtype=">u4").astype(np.int64)
        return None

    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            end = f.tell()
            f.seek(0)
            return walk(f, end, {})
    except (OSError, struct.error):
        return None


def _ffprobe_sample_sizes(path: Path) -> np.ndarray | None:
    ffprobe = shutil.which("ffprobe")
    bundled = Path(__file__).parent.parent / "ffmpeg" / "ffprobe.exe"
    if bundled.exists():
        ffprobe = str(bundled)
    if ffprobe is None:
        return None

    out = subprocess.run(
        [ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "packet=dts,size", "-of", "csv=p=0",
         str(path)],
        capture_output=True, text=True,
    )
    if out.returncode != 0:
        return None

    packets = []
    for line in out.stdout.splitlines():
        dts, size = line.split(",")[:2]
        packets.append((int(dts), int(size)))
    packets.sort()
    return np.array([size for _, size in packets], dtype=np.int64)


def match_packets(sidecar: Sidecar, sizes: np.ndarray) -> int:
    """Where the video's first packet sits in the sidecar's packet log.

    Muxing can change every packet's size by the same few bytes - mp4 drops av1's temporal delimiters - so a
    match is where nearly every size differs from the log's by one amount.
    """
    logged = sidecar.packets["size"].astype(np.int64)
    count = len(sizes)

    for first in range(len(logged) - count, -1, -1):
        difference = logged[first : first + count] - sizes
        share = np.count_nonzero(difference == np.median(difference)) / count
        if share >= MATCH_SHARE:
            return first

    raise LogError("the video doesn't match the frame timing log")


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


def read_times(sidecar: Sidecar, sizes: np.ndarray) -> np.ndarray:
    """When OBS's GPU read the captured picture for each of the video's frames, in seconds from the save."""
    frames = len(sizes)
    first = match_packets(sidecar, sizes)
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
        raise LogError("the log has no reads - is the Frame Timing Probe filter on the game capture?")
    if missing.any():
        read[missing] = tick_time[missing] + np.nanmedian(read - tick_time)

    return read


def game_frames(read: np.ndarray, presents: Presents, fps: Fraction) -> np.ndarray:
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


def moments(game: np.ndarray, presents: Presents) -> np.ndarray:
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
        out.props[deduplicate.PROP_FRAMES] = frames_list[begin : stop + 1]
        out.props[deduplicate.PROP_TIMES] = times_list[begin : stop + 1]
        out.props[deduplicate.PROP_TIME_SCALE] = TIME_SCALE
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
    window = [deduplicate.shifted(diffs, offset) for offset in offsets]
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
        out.props[deduplicate.PROP_FRAMES] = frames_list[begin : stop + 1]
        out.props[deduplicate.PROP_TIMES] = times_list[begin : stop + 1]
        out.props[deduplicate.PROP_TIME_SCALE] = TIME_SCALE
        return out

    return core.std.ModifyFrame(holder, [holder, *window], decide), REPEAT_REACH


def analyse(
    clip: vs.VideoNode,
    fps: Fraction,
    start: int,
    length: int,
    video_path: Path,
    repeat_threshold: float,
) -> deduplicate.Dedupe | None:
    """Set up retiming from a video's frame timing log, or None if it has none that fits.

    `clip` is the whole video, `fps` its framerate before any timescale, and what's retimed is the `length`
    frames of it from `start`. `repeat_threshold` is deduplication's threshold, used to find repeats when the
    log doesn't have the game's frames.
    """
    sidecar_path = video_path.with_name(video_path.name + SIDECAR_SUFFIX)
    if not sidecar_path.exists():
        return None

    try:
        sidecar = load_sidecar(sidecar_path)

        sizes = _mp4_sample_sizes(video_path)
        if sizes is None or len(sizes) != clip.num_frames:
            sizes = _ffprobe_sample_sizes(video_path)
        if sizes is None:
            raise LogError("couldn't read the video's packet sizes")
        if len(sizes) != clip.num_frames:
            raise LogError(f"the video has {len(sizes)} packets but {clip.num_frames} frames")

        read = read_times(sidecar, sizes)

        presents, missing_reason = sidecar_presents(sidecar)
    except (LogError, OSError, ValueError, KeyError) as e:
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

    return deduplicate.Dedupe(
        decisions=decisions,
        length=length,
        max_gap=max_gap,
        timing="frame timing log",
        resolution=1,
        slots=SLOTS,
    )
