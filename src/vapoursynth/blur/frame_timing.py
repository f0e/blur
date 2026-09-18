"""Frame timing - putting frames back where the game really drew them.

A game running at a different rate to the recording doesn't get captured at even intervals. Recording a 500fps
game at 360fps, each captured frame is whichever game frame finished last, so sometimes one game frame has gone
by since the previous capture and sometimes two. Motion that was perfectly steady in the game comes out as
steps of one size and then double that, and interpolating between the frames as though they were evenly spaced
keeps the unevenness in.

Knowing the game's framerate isn't enough on its own to say which captures got two game frames. That depends on
where the capture lands relative to the game's frames, and when the two clocks sit close to a frame boundary,
fractions of a millisecond decide it. So the count is read from the picture instead: the average motion
between captures is measured, the motion one game frame makes is worked out from the captures around it
(which between them must span `frames * game fps / recording fps` game frames), and each capture's motion
divided by that is how many game frames it moved.

A count the measurement isn't sure of - one that isn't close to a whole number, or isn't a count the framerates
allow - is worked out from the ones around it instead. A capture always shows the game frame drawn last before
it, so its position on the game's timeline can only be up to a game frame behind where even spacing would put
it. Of the counts it could be, the one that keeps its neighbours inside that game frame is the right one.

The counts place every frame on the game's timeline, which is handed to interpolation the same way
deduplication hands over its own - see blur/deduplicate.py - so every output frame is generated from the two
real frames either side of the moment it shows. A frame that repeats the one before it moved zero game frames
and simply drops out, which covers frames the recording duplicated too. Where nothing is moving frames are
assumed to be evenly spaced - with nothing moving, their timing can't be seen anyway.

Placing one frame takes the motion of around a hundred and fifty frames either side of it. Asking vapoursynth
for those per frame means decoding them over and over once they fall out of its cache, which gets slower the
further a render goes, so motion is measured a block at a time instead - each block decoded once, in order,
and kept on disk - and frames are placed from the numbers.
"""

import vapoursynth as vs
from vapoursynth import core

import bisect
import hashlib
import math
import struct
from fractions import Fraction
from pathlib import Path

import blur.deduplicate as deduplicate
import blur.utils as u
from blur import log

# motion is measured on a copy of the video scaled down to this height, which is plenty to tell one game frame
# of movement from two and much cheaper to search
MEASURE_HEIGHT = 720
MEASURE_BLOCKSIZE = 16

# average motion below this (in the mask's units, where a still picture reads around 0.003) is too little to
# count game frames from
MIN_UNIT = 0.1

# how far from a whole number a measured count can be and still be believed
COUNT_TOLERANCE = 0.3

# how far from the average a count can be. the capture landing a little earlier or later against the game's
# frames moves a count by one at most
COUNT_RANGE = 1.5

# captures either side used to work out how far one game frame moves the picture. a wide first pass gets the
# scale roughly right, then a narrow second pass over the counts that gave follows the motion speeding up and
# slowing down
UNIT_RADIUS = 24
FINE_UNIT_RADIUS = 8

# captures either side whose counts decide one that couldn't be measured
FILL_RADIUS = 12

# counting game frames only gives times relative to each other, so each frame is placed against the average
# over this many captures either side. that also follows the two clocks drifting apart if the game's framerate
# isn't quite what was given
CENTER_RADIUS = 24

# a run of repeated frames longer than this isn't a recording catching up
MAX_REPEATS = 8

# a frame's position is kept to this fraction of a frame
TIME_SCALE = 1 << 16

# a decision spans one frame of the timeline, which normally holds one real frame landing partway through it
# and so two pairs. one more leaves room for a count that was still got wrong
SLOTS = 3

# motion is measured this many frames at a time. has to be more than twice as long as a decision reaches
BLOCK = 256

# measured blocks are kept here, under the settings folder. a block is a couple of kilobytes, so this many is
# a few megabytes, or a couple of hours of 360fps footage
CACHE_FOLDER = "frame-timing-cache"
CACHE_LIMIT = 4096
CACHE_SUFFIX = ".motion"

PROP_MOTION = "BlurMotion"
PROP_BLOCK = "BlurMotionBlock"


def _parse(value: str) -> Fraction | None:
    value = value.strip()
    if not value:
        return None

    try:
        fps = Fraction(value).limit_denominator(1000000)
    except ValueError:
        raise u.BlurException(f"Game FPS is not a number: '{value}'")

    if fps <= 0:
        raise u.BlurException("Game FPS must be above 0")

    return fps


def _motion(clip: vs.VideoNode) -> vs.VideoNode:
    """A clip whose frame n measures how far the picture moved between frames n and n + 1."""
    height = min(MEASURE_HEIGHT, clip.height)
    width = max(MEASURE_BLOCKSIZE, round(clip.width * height / clip.height / 2) * 2)

    resize_args = {}
    if clip.format.color_family == vs.RGB:
        resize_args["matrix_s"] = "709"

    small = core.resize.Bilinear(clip, width, height, format=vs.GRAY8, **resize_args)

    super = core.mv.Super(small, pel=1, hpad=MEASURE_BLOCKSIZE, vpad=MEASURE_BLOCKSIZE)
    vectors = core.mv.Analyse(super, isb=False, blksize=MEASURE_BLOCKSIZE)

    # the mask is proportional to how far each block moved since the frame before. a scene change reads as
    # full scale so it can be told apart from a picture that didn't move
    mask = core.mv.Mask(small, vectors, kind=0, ml=100, ysc=255)
    stats = core.std.PlaneStats(mask)

    return deduplicate.shifted(stats, 1)


class _BlockCache:
    """Measured blocks on disk, named for the video, the block, and this module's source.

    Any failure to read or write just means measuring again - a cache that isn't working is not a reason to
    fail a render.
    """

    def __init__(self, video_path: Path, folder: Path):
        self.folder = folder
        self.prefix = None

        try:
            stat = video_path.stat()
            identity = (
                f"{video_path.resolve()}\n{stat.st_size}\n{stat.st_mtime_ns}\n{BLOCK}"
            ).encode()
            key = hashlib.sha1(identity + Path(__file__).read_bytes()).hexdigest()[:16]
            readable = "".join(
                c if c.isalnum() or c in "-_" else "_" for c in video_path.stem
            )[:48]

            folder.mkdir(parents=True, exist_ok=True)
            self.prefix = f"{readable}-{key}"
        except OSError as e:
            log.info(f"frame timing: can't use the cache ({e})")

    def _path(self, block: int) -> Path:
        return self.folder / f"{self.prefix}-{block}{CACHE_SUFFIX}"

    def load(self, block: int) -> list[float] | None:
        if self.prefix is None:
            return None

        try:
            data = self._path(block).read_bytes()
        except OSError:
            return None

        if len(data) != BLOCK * 8:
            return None

        return list(struct.unpack(f"<{BLOCK}d", data))

    def store(self, block: int, values: list[float]):
        if self.prefix is None:
            return

        path = self._path(block)
        try:
            # written alongside and moved into place, so a half written file is never picked up
            partial = path.with_suffix(".partial")
            partial.write_bytes(struct.pack(f"<{BLOCK}d", *values))
            partial.replace(path)

            if block == 0:
                self.prune()
        except OSError as e:
            log.info(f"frame timing: couldn't save measurements ({e})")

    def prune(self):
        kept = sorted(
            (p for p in self.folder.iterdir() if p.suffix == CACHE_SUFFIX),
            key=lambda p: p.stat().st_mtime,
            reverse=True,
        )

        for stale in kept[CACHE_LIMIT:]:
            stale.unlink(missing_ok=True)


def _blocks(clip: vs.VideoNode, cache: _BlockCache) -> vs.VideoNode:
    """A clip whose frame b carries the motion measured across block b of `clip`.

    Values are NaN where there's nothing to measure - past the last frame, or across a scene change.
    """
    length = clip.num_frames
    last = length - 1
    count = math.ceil(length / BLOCK)

    holder = core.std.BlankClip(width=1, height=1, format=vs.GRAY8, length=count, keep=True)

    motion = _motion(clip)
    parts = [
        core.std.SelectEvery(deduplicate.shifted(motion, k), BLOCK, 0)
        for k in range(BLOCK)
    ]

    known: dict[int, list[float]] = {}

    def measure(n: int, f: list[vs.VideoFrame]) -> vs.VideoFrame:
        values = []
        for k in range(BLOCK):
            value = math.nan
            if n * BLOCK + k < last:
                value = float(f[1 + k].props["PlaneStatsAverage"]) * 255
                if value >= 255:
                    value = math.nan
            values.append(value)

        known[n] = values
        cache.store(n, values)

        out = f[0].copy()
        out.props[PROP_MOTION] = values
        out.props[PROP_BLOCK] = n
        return out

    measured = core.std.ModifyFrame(holder, [holder, *parts], measure)

    def pick(n: int) -> vs.VideoNode:
        # decided before anything is requested, so a block that's already known never touches the video
        values = known.get(n)
        if values is None:
            values = cache.load(n)
            if values is None:
                return measured
            known[n] = values

        return core.std.SetFrameProps(holder, **{PROP_MOTION: values, PROP_BLOCK: n})

    return core.std.FrameEval(holder, pick)


def analyse(
    clip: vs.VideoNode,
    game_fps: str,
    recording_fps: Fraction,
    start: int,
    length: int,
    video_path: Path,
    settings_path: Path,
) -> deduplicate.Dedupe | None:
    """Set up retiming onto the timeline of a game running at `game_fps`, or None if it isn't set.

    `clip` is the whole video, and what's retimed is the `length` frames of it from `start` - measuring outside
    the trimmed part keeps frames near its ends placed the same as they would be in a full render.
    `recording_fps` is the framerate the video was really captured at, before any timescale was applied.
    """
    fps = _parse(game_fps)
    if fps is None:
        return None

    rate = float(fps / Fraction(recording_fps))

    log.info(
        f"retiming to a game running at {float(fps):g}fps "
        f"({rate:.3f} game frames per recorded frame)"
    )

    last = clip.num_frames - 1
    end = start + length - 1

    # how far either side of itself a decision looks for real frames. a frame lands up to a game frame behind
    # its own slot, which is more than one slot when the game runs slower than the recording
    search = max(3, math.ceil(2 / rate) + 1)

    # counts are needed this far out to place the frames a decision looks at
    count_reach = search + CENTER_RADIUS
    reach = count_reach + FILL_RADIUS + FINE_UNIT_RADIUS + MAX_REPEATS + UNIT_RADIUS + 1

    if 2 * reach >= BLOCK:
        raise u.BlurException(
            f"Game FPS is too low for this recording ({rate:.3f} game frames per recorded frame)"
        )

    blocks = _blocks(clip, _BlockCache(video_path, settings_path / CACHE_FOLDER))
    spread = core.std.Interleave([blocks] * BLOCK)

    # the blocks either end of a decision's reach, and the one it's in. a reach is shorter than a block, so
    # between them that's every block it touches
    windows = [
        deduplicate.shifted(spread, start + offset)[:length]
        for offset in (-reach, 0, reach)
    ]

    holder = core.std.BlankClip(width=1, height=1, format=vs.GRAY8, length=length, keep=True)

    def allowed(count: int, repeats: int) -> bool:
        # a recording that repeated frames catches up with one that moved all of theirs at once
        return count == 0 or abs(count - (repeats + 1) * rate) < COUNT_RANGE

    def decide(n: int, f: list[vs.VideoFrame]) -> vs.VideoFrame:
        source = start + n
        low = source - reach

        values: dict[int, list] = {}
        for frame in f[1:]:
            values[int(frame.props[PROP_BLOCK])] = frame.props[PROP_MOTION]

        # motion from frame i to i + 1, or None where there's no such pair or it's a scene change
        moved: list[float | None] = []
        for i in range(low, source + reach + 1):
            value = None
            if 0 <= i < last:
                block = values.get(i // BLOCK)
                if block is not None:
                    value = float(block[i % BLOCK])
                    if math.isnan(value):
                        value = None
            moved.append(value)

        # running totals, so the average over any stretch is a subtraction
        known_total = [0.0]
        known_count = [0]
        for value in moved:
            known_total.append(known_total[-1] + (value or 0.0))
            known_count.append(known_count[-1] + (value is not None))

        # first pass: counts against the average motion of a game frame over a wide window
        rough_low = low + UNIT_RADIUS
        rough: list[int | None] = []
        for i in range(rough_low, source + reach - UNIT_RADIUS + 1):
            value = moved[i - low]
            a, b = i - UNIT_RADIUS - low, i + UNIT_RADIUS + 1 - low
            count = known_count[b] - known_count[a]
            unit = (known_total[b] - known_total[a]) / (count * rate) if count else 0
            rough.append(None if value is None or unit < MIN_UNIT else round(value / unit))

        # second pass: the same over a narrow window, scaled by the counts the first pass found in it
        fine_low = rough_low + FINE_UNIT_RADIUS + MAX_REPEATS
        counts: list[int | None] = []
        measured: list[float | None] = []
        for i in range(fine_low, source + count_reach + FILL_RADIUS):
            value = moved[i - low]
            total = 0.0
            steps = 0
            for j in range(i - FINE_UNIT_RADIUS, i + FINE_UNIT_RADIUS + 1):
                if rough[j - rough_low] is not None:
                    total += moved[j - low]
                    steps += rough[j - rough_low]

            repeats = 0
            while repeats < MAX_REPEATS and rough[i - 1 - repeats - rough_low] == 0:
                repeats += 1

            count = None
            ratio = None
            if value is not None and steps > 0 and total / steps >= MIN_UNIT:
                ratio = value / (total / steps)
                nearest = round(ratio)
                if abs(ratio - nearest) <= COUNT_TOLERANCE and allowed(nearest, repeats):
                    count = nearest

            counts.append(count)
            measured.append(ratio)

        def count_at(i: int) -> float:
            """The count between frames i and i + 1, filled in from its neighbours if it wasn't measured."""
            count = counts[i - fine_low]
            if count is not None:
                return count

            neighbours = counts[i - FILL_RADIUS - fine_low : i + FILL_RADIUS + 1 - fine_low]
            if all(c is None for c in neighbours):
                return rate

            # positions of the frames around i relative to i's own, with unmeasured counts taken as average
            behind = [0.0]
            for j in range(i - 1, i - FILL_RADIUS - 1, -1):
                other = counts[j - fine_low]
                behind.append(behind[-1] - ((rate if other is None else other) - rate))

            ahead = [0.0]
            for j in range(i + 1, i + FILL_RADIUS + 1):
                other = counts[j - fine_low]
                ahead.append(ahead[-1] + (rate if other is None else other) - rate)

            ratio = measured[i - fine_low]
            best = None
            for candidate in range(0, math.ceil(rate) + 2):
                shift = candidate - rate
                positions = behind + [shift + a for a in ahead]
                width = max(positions) - min(positions)
                miss = abs(candidate - (rate if ratio is None else ratio))
                if best is None or (width, miss) < best[0]:
                    best = ((width, miss), candidate)

            return best[1]

        # position on the game's timeline, relative to where frames would be if they were evenly spaced
        first = source - count_reach
        drift = [0.0]
        repeated = []
        for i in range(first, source + count_reach):
            step = count_at(i)
            drift.append(drift[-1] + step - rate)
            repeated.append(step == 0)

        drift_total = [0.0]
        for i, value in enumerate(drift):
            drift_total.append(drift_total[-1] + (value if 0 <= first + i <= last else 0.0))

        def placed(j: int) -> float:
            a = max(j - CENTER_RADIUS, 0) - first
            b = min(j + CENTER_RADIUS, last) + 1 - first
            average = (drift_total[b] - drift_total[a]) / (b - a)

            # against the average a frame sits up to half a game frame either way. it can really only be
            # behind its slot, so the other half goes on top
            return j + (drift[j - first] - average - 0.5) / rate

        # every real frame near this one, first of any repeats, in timeline order
        frames: list[int] = []
        times: list[int] = []
        for j in range(max(start, source - search), min(end, source + search) + 1):
            if j > start and repeated[j - 1 - first]:
                continue

            time = round((placed(j) - start) * TIME_SCALE)
            if times and time <= times[-1]:
                continue

            frames.append(j - start)
            times.append(time)

        if not frames:
            frames, times = [n], [n * TIME_SCALE]

        # keep just the ones whose stretch of the timeline reaches into this frame's
        begin = max(0, bisect.bisect_right(times, n * TIME_SCALE) - 1)
        stop = min(bisect.bisect_left(times, (n + 1) * TIME_SCALE), len(times) - 1)
        stop = min(max(stop, begin), begin + SLOTS)

        out = f[0].copy()
        out.props[deduplicate.PROP_FRAMES] = frames[begin : stop + 1]
        out.props[deduplicate.PROP_TIMES] = times[begin : stop + 1]
        out.props[deduplicate.PROP_TIME_SCALE] = TIME_SCALE

        return out

    decisions = core.std.ModifyFrame(holder, [holder, *windows], decide)

    return deduplicate.Dedupe(
        decisions=decisions,
        length=length,
        max_gap=max(2, math.ceil(2 / rate)),
        timing="game fps",
        resolution=1,
        slots=SLOTS,
    )
