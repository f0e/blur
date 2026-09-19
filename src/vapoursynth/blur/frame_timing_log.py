"""Reading the frame timing log a recording comes with.

The obs-frame-timing-recorder OBS plugin writes a `.frametiming` sidecar next to each recording and replay.
It's a small binary file: a header, then batches of fixed size records, one tag at a time. A replay writes one
batch per tag; a recording appends more as it runs, so a tag can turn up several times and the last batch is
short if OBS died mid-recording.

What's in it:

- `TICK`  - every frame OBS rendered, with the timestamp it gave that frame
- `READ`  - when OBS's GPU actually read the captured picture, measured by the plugin's probe filter, and a
            hash of the picture it read
- `PCKT`  - every encoded video packet, which is what lets a saved file be lined up with the log
- `PRES`  - the captured game's own frames, when the plugin had permission to trace them
- `GAME`  - which process OBS was capturing, how, and on what interval its hook copied frames

This module only reads the file and works out the game's frames from it. Turning that into a timeline is
blur/frame_timing.py's job.
"""

import shutil
import struct
import subprocess
from collections import Counter
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path

import numpy as np

SIDECAR_SUFFIX = ".frametiming"
SIDECAR_VERSION = 7

# the share of a video's packets whose size has to differ from the log's by the same amount to match it
MATCH_SHARE = 0.98

# how many packet sizes in a row have to agree for an offset to be worth checking properly, and where in the
# video those runs are taken from
MATCH_RUN = 8
MATCH_ANCHORS = 3

HEADER = struct.Struct("<8sIIqqIII")
BATCH = struct.Struct("<4sI")

TICK = np.dtype([("qpc", "<i8"), ("frame_time", "<u8"), ("total_frames", "<u8"), ("lagged_frames", "<u8")])
READ = np.dtype([
    ("frame_time", "<u8"),
    ("submitted_qpc", "<i8"),
    ("done_qpc", "<i8"),
    ("fingerprint", "<u8"),
])
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
    ("swap_chain", "<u8"),
    ("process_id", "<u8"),
    ("runtime", "<u8"),
    ("present_mode", "<u8"),
    ("final_state", "<u8"),
    ("flags", "<u8"),
    ("app_sim_start", "<u8"),
    ("app_sim_end", "<u8"),
    ("reflex_sim_start", "<u8"),
    ("reflex_sim_end", "<u8"),
    ("screen_time", "<u8"),
    ("frame_type", "<u8"),
    ("window", "<u8"),
])
GAME = np.dtype([
    ("process_id", "<u8"),
    ("capture", "<u8"),
    ("window", "<u8"),
    ("flags", "<u8"),
    ("frame_interval", "<u8"),
    ("name", "S120"),
])

DTYPES = {b"TICK": TICK, b"READ": READ, b"PCKT": PACKET, b"PRES": PRESENT, b"GAME": GAME}

# how obs was capturing, which says when a game frame became something it could read
HOOK_CAPTURE = 0
WINDOW_CAPTURE = 1

# the capture source's settings that change the model
CAPTURE_LIMIT_FRAMERATE = 1 << 0
CAPTURE_SHARED_MEMORY = 1 << 1

PRESENT_FAILED = 1 << 1

# presentdata's FrameType: what the compositor showed. anything above this was generated rather than drawn
FRAME_TYPE_APPLICATION = 2

# why a log has no game frames, from the plugin's point of view
GAME_TIMING = {1: "OBS had no permission to trace them", 2: "tracing them failed", 3: "tracing never started"}


class LogError(Exception):
    pass


@dataclass
class Sidecar:
    qpc_frequency: int
    saved_qpc: int
    fps: Fraction
    # how many ticks after the tick whose timestamp a frame carries the frame was rendered, which depends on
    # whether the output's encoder takes textures or raw frames. the plugin works it out from the encoder
    render_delay: int
    ticks: np.ndarray
    reads: np.ndarray
    packets: np.ndarray
    presents: np.ndarray
    games: np.ndarray
    game_timing: int

    def seconds(self, qpc: np.ndarray) -> np.ndarray:
        """QPC values as seconds from the log's zero, which is when it was saved."""
        return (qpc.astype(np.int64) - self.saved_qpc) / self.qpc_frequency

    def qpc(self, seconds: float) -> int:
        return int(self.saved_qpc + seconds * self.qpc_frequency)


@dataclass
class Presents:
    """The captured game's own frames, in seconds from when the sidecar was saved."""

    process: str
    start: np.ndarray
    present: np.ndarray
    gpu_start: np.ndarray
    gpu_done: np.ndarray
    # when obs could first read each frame
    visible: np.ndarray
    # when each frame was simulated, and what said so
    simulated: np.ndarray
    simulated_by: str
    # the interval the hook skips presents on, in seconds, or 0 when nothing skips them
    interval: float


def sidecar_path(video_path: Path) -> Path:
    """Where a video's log would be, if it has one."""
    return video_path.with_name(video_path.name + SIDECAR_SUFFIX)


def load_sidecar(path: Path) -> Sidecar:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise LogError(f"{path.name} is too short to be a frame timing log")

    magic, version, game_timing, frequency, saved, num, den, render_delay = HEADER.unpack_from(data)
    if magic != b"BLURFTIM":
        raise LogError(f"{path.name} isn't a frame timing log")
    if version != SIDECAR_VERSION:
        raise LogError(f"{path.name} is a version {version} log, and this version of blur reads {SIDECAR_VERSION}")
    if not frequency or not num or not den:
        raise LogError(f"{path.name} has no clock or framerate in its header")

    batches: dict[bytes, list[np.ndarray]] = {tag: [] for tag in DTYPES}
    offset = HEADER.size
    while offset + BATCH.size <= len(data):
        tag, count = BATCH.unpack_from(data, offset)
        offset += BATCH.size
        dtype = DTYPES.get(tag)
        if dtype is None:
            raise LogError(f"{path.name} has a {tag!r} batch, which this version of blur doesn't know")
        # a batch cut short by a crash is kept as far as it got
        count = min(count, (len(data) - offset) // dtype.itemsize)
        batches[tag].append(np.frombuffer(data, dtype, count, offset))
        offset += count * dtype.itemsize

    sections = {tag: np.concatenate(parts) if parts else np.empty(0, DTYPES[tag]) for tag, parts in batches.items()}
    if len(sections[b"TICK"]) == 0 or len(sections[b"PCKT"]) == 0:
        raise LogError(f"{path.name} has no ticks or no packets")

    return Sidecar(
        frequency,
        saved,
        Fraction(num, den),
        render_delay,
        sections[b"TICK"],
        sections[b"READ"],
        sections[b"PCKT"],
        sections[b"PRES"],
        sections[b"GAME"],
        game_timing,
    )


def captured_game(sidecar: Sidecar, process: int) -> np.ndarray | None:
    """What the log says about how obs was capturing a process, if it says anything."""
    captured = sidecar.games[sidecar.games["process_id"] == process]
    return captured[-1] if len(captured) else None


def game_presents(sidecar: Sidecar, first: float, last: float) -> tuple[Presents | None, str]:
    """The game's frames over the seconds from `first` to `last`, or why there aren't any.

    The log holds the captured game's presents and nothing else; its busiest swapchain is the captured one.
    Game capture hooks the game and gets its frames as it presents them, so a frame is readable once the GPU
    has finished it. Window capture is handed the window by the compositor instead, so a frame is readable
    when it reaches the screen, and one that never got there was never captured.

    Everything is decided over the stretch the clip needs rather than over the whole log, which for a
    recording hours long is a different question.
    """
    records = sidecar.presents
    if len(records) == 0:
        return None, GAME_TIMING.get(sidecar.game_timing, "the log doesn't have them")

    # frames from a little before the clip, so its first frames have something to look back at
    within = (records["present_start"] >= sidecar.qpc(first)) & (records["present_start"] <= sidecar.qpc(last))
    records = records[within & ((records["flags"] & PRESENT_FAILED) == 0)]
    if len(records) < 2:
        return None, "the log has none of them over this part of the video"

    # a game that restarted mid-log presents under a new id, and the one that presented most is the one shown
    process = Counter(int(p) for p in records["process_id"]).most_common(1)[0][0]
    records = records[records["process_id"] == process]
    captured = captured_game(sidecar, process)

    hooked = captured is None or int(captured["capture"]) == HOOK_CAPTURE
    if captured is not None and int(captured["flags"]) & CAPTURE_SHARED_MEMORY:
        # the hook copies into shared memory and obs uploads it on its tick, so a frame isn't readable when
        # the gpu finishes it and none of the rest holds either
        return None, "obs was capturing in compatibility mode, which this model doesn't cover"

    # the capture was pointed at one window, so presents to another one aren't in the recording
    window = int(captured["window"]) if captured is not None else 0
    if window and np.any(records["window"] == window):
        records = records[records["window"] == window]

    chain = Counter(int(c) for c in records["swap_chain"]).most_common(1)[0][0]
    records = records[records["swap_chain"] == chain]
    records = records[np.argsort(records["present_start"], kind="stable")]
    if len(records) < 2:
        return None, "the log has none of them over this part of the video"

    if hooked:
        # placing frames at their presents instead of at gpu done is worse than not placing them at all
        if np.mean(records["ready"] > 0) < 0.5:
            return None, "the log has no gpu timing for them"
    else:
        shown = records["screen_time"] > 0
        if np.mean(shown) < 0.5:
            return None, "the log doesn't say when its frames reached the screen"
        # a frame the compositor dropped never reached the capture, and one it generated isn't the game's
        records = records[shown & (records["frame_type"] <= FRAME_TYPE_APPLICATION)]
        if len(records) < 2:
            return None, "the log has none of them over this part of the video"

    present_qpc = records["present_start"].astype(np.int64)
    ready_qpc = records["ready"].astype(np.int64)

    # a frame starts when the game comes back from presenting the one before, as presentmon has it
    ended = present_qpc + records["time_in_present"].astype(np.int64)
    start_qpc = np.concatenate([[present_qpc[0]], ended[:-1]])

    # the odd frame without gpu timing is taken to have started and finished when it was presented
    done_qpc = np.where(ready_qpc > 0, ready_qpc, present_qpc)
    gpu_start_qpc = records["gpu_start"].astype(np.int64)
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

    # the hook's own interval, which obs works out from its framerate and whether the capture limits it
    interval = float(captured["frame_interval"]) / 1e9 if hooked and captured is not None else 0.0

    # the first frame has no previous present to start from, so it's dropped
    def seconds(qpc: np.ndarray) -> np.ndarray:
        return sidecar.seconds(qpc)[1:]

    name = captured["name"].decode(errors="replace") if captured is not None else f"process {process}"
    presents = Presents(
        name,
        seconds(start_qpc),
        seconds(present_qpc),
        seconds(gpu_start_qpc),
        seconds(done_qpc),
        seconds(visible_qpc),
        seconds(simulated_qpc),
        simulated_by,
        interval,
    )
    return presents, ""


def render_ticks(sidecar: Sidecar, sizes: np.ndarray) -> np.ndarray:
    """For each frame of the video, the tick whose render drew it.

    A packet carries `cts`, the timestamp obs gave the frame, which is the same number the tick log carries -
    so the two line up exactly rather than by rounding microseconds. The frame was rendered `render_delay`
    ticks after the tick that stamp belongs to; a frame obs duplicated because a tick ran long is stamped on
    a slot no tick occupies, and lands on the tick that really drew it.
    """
    first = match_packets(sidecar, sizes)
    packets = sidecar.packets[first : first + len(sizes)]
    packets = packets[np.argsort(packets["pts"], kind="stable")]

    cts = packets["cts"].astype(np.int64)
    pts = packets["pts"].astype(np.int64)
    known = np.flatnonzero(cts > 0)
    if len(known) == 0:
        raise LogError("the log's packets have no obs timestamps")

    if len(known) < len(cts):
        # obs didn't give timing for a packet: stamps step by one frame per packet, so its neighbour says
        steps = np.diff(cts[known]) / np.diff(pts[known])
        step = np.median(steps[np.isfinite(steps)])
        nearest = known[np.clip(np.searchsorted(known, np.arange(len(cts))), 0, len(known) - 1)]
        cts = np.where(cts > 0, cts, cts[nearest] + np.round((pts - pts[nearest]) * step).astype(np.int64))

    tick_time = sidecar.ticks["frame_time"].astype(np.int64)
    at = np.searchsorted(tick_time, cts, "right") - 1
    return np.clip(at + sidecar.render_delay, 0, len(tick_time) - 1)


def reads_for(sidecar: Sidecar, ticks: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """For each frame of the video, when obs's GPU finished reading its picture, in seconds from the log's
    zero, and the fingerprint of the picture it read (0 where there isn't one)."""
    reads = sidecar.reads
    reported = reads["done_qpc"] > 0
    if not np.any(reported):
        raise LogError("the log has no reads - is the Frame Timing Probe filter on the game capture?")

    reads = reads[reported]
    reads = reads[np.argsort(reads["frame_time"], kind="stable")]
    logged = reads["frame_time"].astype(np.int64)

    wanted = sidecar.ticks["frame_time"].astype(np.int64)[ticks]
    at = np.searchsorted(logged, wanted)
    found = at < len(reads)
    at = np.minimum(at, len(reads) - 1)
    found &= logged[at] == wanted

    read = np.where(found, sidecar.seconds(reads["done_qpc"][at]), np.nan)
    fingerprint = np.where(found, reads["fingerprint"][at], np.uint64(0))

    # a tick the probe didn't report - obs wasn't drawing the capture, or it ran out of events - is taken to
    # have read as late as reads usually do. nothing is known about its picture
    missing = np.isnan(read)
    if missing.any():
        tick = sidecar.seconds(wanted)
        read[missing] = tick[missing] + np.nanmedian(read - tick)

    return read, fingerprint


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


def packet_sizes(video_path: Path, frames: int) -> np.ndarray:
    """The size of every encoded packet in a video, which is how it's matched to a log.

    The sample table is read directly where it can be, since that's far quicker than starting ffprobe - but it
    only covers regular mp4s, so hybrid mp4 and mkv fall back.
    """
    sizes = _mp4_sample_sizes(video_path)
    if sizes is None or len(sizes) != frames:
        sizes = _ffprobe_sample_sizes(video_path)

    if sizes is None:
        raise LogError("couldn't read the video's packet sizes")
    if len(sizes) != frames:
        raise LogError(f"the video has {len(sizes)} packets but {frames} frames")

    return sizes


def match_packets(sidecar: Sidecar, sizes: np.ndarray) -> int:
    """Where the video's first packet sits in the sidecar's packet log.

    Muxing can change every packet's size by the same few bytes - mp4 drops av1's temporal delimiters - so
    what matches exactly is the *difference* between one packet and the next, and a match is where nearly
    every size differs from the log's by one amount.

    A run of differences is looked for from a few places in the video, which finds the handful of offsets
    worth checking properly however the muxer treated any one packet.
    """
    logged = sidecar.packets["size"].astype(np.int64)
    count = len(sizes)
    room = len(logged) - count
    if room < 0:
        raise LogError(f"the log has {len(logged)} packets, fewer than the video's {count}")

    def share(first: int) -> float:
        difference = logged[first : first + count] - sizes
        return float(np.unique(difference, return_counts=True)[1].max() / count)

    steps = np.diff(sizes)
    logged_steps = np.diff(logged)
    run = min(MATCH_RUN, len(steps))

    best = (0.0, -1)
    for anchor in np.linspace(0, max(len(steps) - run, 0), MATCH_ANCHORS, dtype=np.int64):
        if run == 0:
            break

        # the run can only sit where the whole video still fits around it
        wanted = steps[anchor : anchor + run]
        found = anchor + np.flatnonzero(logged_steps[anchor : anchor + room + 1] == wanted[0])
        for step in range(1, run):
            if len(found) <= 1:
                break
            found = found[logged_steps[found + step] == wanted[step]]

        for offset in found - anchor:
            if 0 <= offset <= room:
                matched = share(int(offset))
                if matched > best[0]:
                    best = (matched, int(offset))

        if best[0] >= MATCH_SHARE:
            return best[1]

    raise LogError("the video doesn't match the frame timing log")
