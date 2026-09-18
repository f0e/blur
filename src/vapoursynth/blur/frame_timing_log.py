"""Reading the frame timing log a recording comes with.

The obs-frame-timing-recorder OBS plugin writes a `.frametiming` sidecar next to each recording and replay.
It's a small binary file: a header, then batches of fixed size records, one tag at a time. A replay writes one
batch per tag; a recording appends more as it runs, so a tag can turn up several times and the last batch is
short if OBS died mid-recording.

What's in it:

- `TICK`  - every frame OBS rendered, with the timestamp it gave that frame
- `READ`  - when OBS's GPU actually read the captured picture, measured by the plugin's probe filter
- `PCKT`  - every encoded video packet, which is what lets a saved file be lined up with the log
- `PRES`  - the captured game's own frames, when the plugin had permission to trace them
- `GAME`  - which process OBS was capturing, and whether by game capture or window capture

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
SIDECAR_VERSION = 6

# the share of a video's packets whose size has to differ from the log's by the same amount to match it
MATCH_SHARE = 0.98

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

DTYPES = {b"TICK": TICK, b"READ": READ, b"PCKT": PACKET, b"PRES": PRESENT, b"GAME": GAME}

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
    """The captured game's own frames, in seconds from when the sidecar was saved."""

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


def sidecar_path(video_path: Path) -> Path:
    """Where a video's log would be, if it has one."""
    return video_path.with_name(video_path.name + SIDECAR_SUFFIX)


def load_sidecar(path: Path) -> Sidecar:
    data = path.read_bytes()
    magic, version, game_timing, frequency, saved, num, den = HEADER.unpack_from(data)
    if magic != b"BLURFTIM":
        raise LogError(f"{path.name} isn't a frame timing log")
    if version != SIDECAR_VERSION:
        raise LogError(f"{path.name} is a version {version} log, and this version of blur reads {SIDECAR_VERSION}")

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
        sections[b"TICK"],
        sections[b"READ"],
        sections[b"PCKT"],
        sections[b"PRES"],
        sections[b"GAME"],
        game_timing,
    )


def game_presents(sidecar: Sidecar) -> tuple[Presents | None, str]:
    """The game's frames from the plugin's own trace, or why there aren't any.

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

    # the first frame has no previous present to start from, so it's dropped
    def seconds(qpc: np.ndarray) -> np.ndarray:
        return ((qpc - saved) / frequency)[1:]

    name = captured[0]["name"].decode(errors="replace") if len(captured) else f"process {process}"
    presents = Presents(
        name,
        seconds(start_qpc),
        seconds(present_qpc),
        seconds(gpu_start_qpc),
        seconds(done_qpc),
        seconds(visible_qpc),
        hooked,
        seconds(simulated_qpc),
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
