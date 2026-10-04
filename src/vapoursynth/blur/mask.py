"""Masks protect regions of the frame (like a hud) from interpolation and deduplication, by putting the original
frames back over them afterwards.

A mask is an image in <settings path>/masks, white where the frame is interpolated as normal and black where it's
left alone. The auto mask is generated from the video itself (see `measure` and `shape`) and stacked on top.
"""

import hashlib
import itertools
import os
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

import blur.utils as u
import vapoursynth as vs
from blur import log
from vapoursynth import core


def load(path: Path) -> vs.VideoNode:
    """Read a mask image into a one frame GRAY clip."""
    if not path.exists():
        raise u.BlurException(f"Mask '{path.name}' wasn't found in {path.parent}")

    # no index files next to the user's masks, and no progress spam in the render log
    clip = core.bs.VideoSource(source=path, cachemode=0, showprogress=False)

    # take a single plane rather than converting, masks are greyscale anyway
    if clip.format.num_planes > 1:
        clip = core.std.ShufflePlanes(clip, 0, vs.GRAY)

    return clip[0]


# Automatic masks
#
# `measure` samples frames from across the video and finds pixels that stay put in nearly all of them:
#
#  - held still (`_held_still`): the pixel stays the same colour. catches opaque overlays
#  - stands out (`_local_contrast`): the pixel stays on the same side of its surroundings, in brightness or colour.
#    catches translucent overlays like a crosshair. surroundings are the median of the box around it rather than
#    the average, otherwise the scene right next to a bright stroke stands out too and leaves a gap around it
#  - has detail (`_local_contrast`): the pixel differs from its surroundings at all. required as well, so flat
#    areas like empty sky don't get masked (and then stutter when something crosses them)
#
# a sample only counts towards the first two when the area around the pixel changed since the previous sample.
# otherwise a clip that spends most of its time looking at one spot makes that spot look static
#
# the mask is kept tight to the overlay, since masked background stutters and looks worse than the artifacts

# frames sampled from across the clip. each one is a seek and a decode
SAMPLE_COUNT = 24

# the analysis runs at most at this height. every sample is held in memory at once
MAX_ANALYSIS_HEIGHT = 2160

# skipped at each end as a fraction of the clip, for fades and title cards
SAMPLE_MARGIN = 0.02

# samples this similar count as the same frame, and are dropped since they make the scene look static
SAMPLE_DIFFERENCE = 0.012

# how much bigger the sample pool gets when too many samples are near duplicates
SAMPLE_POOL_FACTOR = 3

# no mask is made with fewer usable samples than this
MIN_SAMPLES = 8

# how much of the time a test has to pass. below 1 so one odd sample (a flashbang) doesn't rule a pixel out
MIN_CONSISTENCY = 0.9

# how different a pixel can be between samples and still count as unchanged, to allow for compression noise
SAME_PIXEL_THRESHOLD = 0.03

# the area around a pixel has to change by this much on average (radius at 1080p) for a pair of samples to count
MOVED_RADIUS_PX_1080P = 48
MOVED_THRESHOLD = 0.02

# pixels whose surroundings changed in fewer pairs of samples than this aren't masked, there's too little to go on
MIN_MOVED_PAIRS = 3

# how far a pixel has to be from the median of the box around it to stand out, and the box radius at 1080p. the
# box should be over twice as wide as an overlay's strokes, or the median is the stroke itself
LOCAL_CONTRAST = 0.01
LOCAL_RADIUS_PX_1080P = 3

# how much of the time a pixel has to differ from its surroundings to count as drawn on. far lower than
# MIN_CONSISTENCY since an overlay can blend into a background of the same colour. anything from 0.25-0.75 works
MIN_DETAIL = 0.25

# static pixels without detail are still kept this close (at 1080p) to ones with it, e.g. the middle of a panel
FILL_PX_1080P = 24

# static pixels are dropped unless this much of the box around them is static too. a lone pixel goes, a pair
# stays (for dot crosshairs)
CLUSTER_RADIUS = 2
CLUSTER_COVERAGE = 0.06

# how far the mask grows past what was detected, and how much of that fades out, at 1080p. kept small on purpose
GROW_PX = 1
FEATHER_PX = 1

# only finished masks go in here, so any of them can be copied into the masks folder
CACHE_FOLDER = "auto-masks"

SCORE_CACHE_FOLDER = "auto-mask-cache"

# how many of each to keep, oldest dropped first. masks are tiny, scores aren't
CACHE_LIMIT = 256
SCORE_CACHE_LIMIT = 24

# .nothing marks a video that had nothing worth masking
MASK_SUFFIX = ".png"
NOTHING_SUFFIX = ".nothing"

# minimum static area in pixels at 1080p, just under a dot crosshair
MIN_STATIC_PX_1080P = 16

# more of the frame than this being static means it's not a hud (e.g. a locked off shot), so no mask is made
MAX_STATIC_FRACTION = 0.5


# `measure` is the slow part (reads the video) and only depends on `samples`. `shape` turns its scores into a mask
# and is basically free. scores are cached so changing the other settings doesn't need the video read again


@dataclass(frozen=True)
class Params:
    """The auto mask settings blur exposes, defaulting to the constants above."""

    # measuring
    samples: int = SAMPLE_COUNT

    # shaping. raising stillness masks less
    stillness: float = MIN_CONSISTENCY
    fill: int = FILL_PX_1080P
    padding: int = GROW_PX
    feather: int = FEATHER_PX

    @staticmethod
    def from_settings(settings: dict) -> "Params":
        """Read blur's settings, falling back to a default for anything missing or unreadable."""

        def number(key: str, default, low, high):
            try:
                value = type(default)(settings[key])
            except (KeyError, TypeError, ValueError):
                return default

            return min(max(value, low), high)

        return Params(
            samples=number("auto_mask_samples", SAMPLE_COUNT, 2, 512),
            stillness=number("auto_mask_stillness", MIN_CONSISTENCY, 0.0, 1.0),
            fill=number("auto_mask_fill", FILL_PX_1080P, 0, 512),
            padding=number("auto_mask_padding", GROW_PX, 0, 128),
            feather=number("auto_mask_feather", FEATHER_PX, 0, 128),
        )


DEFAULT_PARAMS = Params()


def _detached(clip: vs.VideoNode) -> vs.VideoNode:
    """Render `clip`'s only frame now and return a clip holding just that, so the analysis can be freed."""
    frame = clip.get_frame(0)

    # not built from `clip`, so there's no reference back to the analysis
    holder = core.std.BlankClip(
        width=clip.width,
        height=clip.height,
        format=clip.format.id,
        length=1,
        keep=True,
    )

    return core.std.ModifyFrame(holder, holder, lambda n, f: frame.copy())


def _sample_indices(num_frames: int, count: int) -> list[int]:
    """Frame numbers spread evenly across the clip, ignoring the margins at each end."""
    first = round((num_frames - 1) * SAMPLE_MARGIN)
    last = round((num_frames - 1) * (1 - SAMPLE_MARGIN))

    count = min(count, last - first + 1)
    if count < 2:
        return []

    step = (last - first) / (count - 1)

    # a set since rounding can land two samples on the same frame in short clips
    return sorted({first + round(i * step) for i in range(count)})


def _differing(analysed: vs.VideoNode, indices: list[int]) -> list[int]:
    """Drop frames too much like the last one kept, so a stretch where nothing moved collapses to one sample."""
    kept = indices[:1]

    for index in indices[1:]:
        difference = core.std.PlaneStats(analysed[kept[-1]], analysed[index])
        if difference.get_frame(0).props["PlaneStatsDiff"] >= SAMPLE_DIFFERENCE:
            kept.append(index)

    return kept


def _samples(analysed: vs.VideoNode, count: int) -> list[int]:
    """Frame numbers to compare: spread across the clip, and different enough from each other to be worth it."""
    indices = _differing(analysed, _sample_indices(analysed.num_frames, count))

    # a few duplicates are fine, more means searching a bigger pool
    if len(indices) >= count * MIN_CONSISTENCY:
        return indices

    pool = _differing(analysed, _sample_indices(analysed.num_frames, count * SAMPLE_POOL_FACTOR))
    if len(pool) <= count:
        return pool

    # spread the survivors back out over the whole clip
    return [pool[round(i * (len(pool) - 1) / (count - 1))] for i in range(count)]


def _analysed(clip: vs.VideoNode) -> vs.VideoNode:
    """Get `clip` ready to measure: float YUV444 (so small things stay a few pixels across in colour), no taller
    than MAX_ANALYSIS_HEIGHT."""
    # full range both sides so differences aren't scaled up
    convert = {"format": vs.YUV444PS, "range_s": "full"}

    if clip.format.color_family == vs.GRAY:
        convert |= {"format": vs.GRAYS, "range_in_s": "full"}
    elif clip.format.color_family == vs.RGB:
        # any matrix works, the values are only compared against each other
        convert |= {"matrix_s": "709"}
    else:
        convert |= {"range_in_s": "full"}

    if clip.height <= MAX_ANALYSIS_HEIGHT:
        return core.resize.Point(clip, **convert)

    # bilinear so thin strokes aren't skipped over
    return core.resize.Bilinear(
        clip,
        width=round(clip.width * MAX_ANALYSIS_HEIGHT / clip.height / 2) * 2,
        height=MAX_ANALYSIS_HEIGHT,
        **convert,
    )


def _plane(clip: vs.VideoNode, index: int) -> vs.VideoNode:
    return core.std.ShufflePlanes(clip, index, vs.GRAY)


def _best_plane(clip: vs.VideoNode) -> vs.VideoNode:
    """Flatten a per-plane score to one GRAY plane, keeping whichever plane is most convinced at each pixel."""
    best = _plane(clip, 0)
    for index in range(1, clip.format.num_planes):
        best = core.std.Expr([best, _plane(clip, index)], "x y max")

    return best


def _moved(brightness: list[vs.VideoNode], radius: int) -> list[vs.VideoNode]:
    """For each pair of consecutive samples, 1 where the area around the pixel changed between them."""
    return [
        core.std.Expr(
            core.std.BoxBlur(core.std.Expr([a, b], "x y - abs"), hradius=radius, vradius=radius),
            f"x {MOVED_THRESHOLD} > 1 0 ?",
        )
        for a, b in itertools.pairwise(brightness)
    ]


def _held_still(brightness: list[vs.VideoNode], moved: list[vs.VideoNode]) -> vs.VideoNode:
    """How many of the pairs that moved each pixel stayed the same in. Compares consecutive pairs so one odd frame
    only affects two pairs."""
    counted = core.std.BlankClip(brightness[0], color=0.0)
    for (a, b), changed in zip(itertools.pairwise(brightness), moved):
        counted = core.std.Expr([counted, a, b, changed], f"y z - abs {SAME_PIXEL_THRESHOLD} < a and x 1 + x ?")

    return counted


def _local_median(clip: vs.VideoNode, radius: int) -> vs.VideoNode:
    """Median of each pixel's box, done as rows then columns. Not exact but close, and far cheaper."""
    size = radius * 2 + 1
    middle = f"sort{size} drop{radius} swap{radius} drop{radius}"

    across = " ".join(f"x[{offset},0]" for offset in range(-radius, radius + 1))
    down = " ".join(f"x[0,{offset}]" for offset in range(-radius, radius + 1))

    clip = core.akarin.Expr(clip, f"{across} {middle}")
    return core.akarin.Expr(clip, f"{down} {middle}")


def _every_plane(gray: vs.VideoNode, like: vs.VideoNode) -> vs.VideoNode:
    """A GRAY clip copied into each plane of `like`'s format, so expressions can combine the two."""
    planes = like.format.num_planes
    if planes == 1:
        return gray

    return core.std.ShufflePlanes([gray] * planes, [0] * planes, like.format.color_family)


def _local_contrast(
    samples: list[vs.VideoNode], radius: int, moved: list[vs.VideoNode]
) -> tuple[vs.VideoNode, vs.VideoNode, vs.VideoNode]:
    """How many of the samples that moved each pixel was on the same side of its neighbours in (stands out), how
    many samples moved, and how much of the time it was on either side (has detail, 0-1). Each plane is measured
    separately and the highest wins. The first sample goes by the first pair."""
    blank = core.std.BlankClip(samples[0], color=[0.0] * samples[0].format.num_planes)
    above, below, either, counted = blank, blank, blank, blank

    for index, sample in enumerate(samples):
        local = _local_median(sample, radius)
        changed = _every_plane(moved[max(0, index - 1)], sample)

        # differences computed inline to save memory, every sample's intermediates are live at once
        above = core.std.Expr([above, sample, local, changed], f"y z - {LOCAL_CONTRAST} > a and x 1 + x ?")
        below = core.std.Expr([below, sample, local, changed], f"y z - -{LOCAL_CONTRAST} < a and x 1 + x ?")
        either = core.std.Expr([either, sample, local], f"y z - abs {LOCAL_CONTRAST} > x 1 + x ?")
        counted = core.std.Expr([counted, changed], "x y +")

    stands_out = _best_plane(core.std.Expr([above, below], "x y max"))
    has_detail = _best_plane(core.std.Expr(either, f"x {len(samples)} /"))

    return stands_out, _plane(counted, 0), has_detail


def measure(clip: vs.VideoNode, samples: int = SAMPLE_COUNT) -> vs.VideoNode | None:
    """Score every pixel of `clip` on how static it is, as an 8 bit GRAY clip: static score on top, detail score
    on the bottom. None when there weren't enough usable samples."""
    log.status("stage", "mask")

    analysed = _analysed(clip)

    indices = _samples(analysed, samples)

    # asking for fewer samples than the minimum still makes a mask
    if len(indices) < min(MIN_SAMPLES, samples):
        log.info(
            f"Mask: only {len(indices)} frames differ from each other, not enough to find a mask. Rendering unmasked"
        )
        return None

    frames = [analysed[i] for i in indices]

    # in analysis pixels
    scale = analysed.height / 1080
    radius = max(1, round(LOCAL_RADIUS_PX_1080P * scale))
    moved_radius = max(1, round(MOVED_RADIUS_PX_1080P * scale))

    # only brightness for _held_still, desaturated scenery would always have the same chroma
    brightness = [_plane(frame, 0) for frame in frames]
    moved = _moved(brightness, moved_radius)

    pairs = core.std.BlankClip(brightness[0], color=0.0)
    for changed in moved:
        pairs = core.std.Expr([pairs, changed], "x y +")

    held = _held_still(brightness, moved)
    stands_out, samples_moved, has_detail = _local_contrast(frames, radius, moved)

    static = core.std.Expr(
        [held, pairs, stands_out, samples_moved],
        f"y {MIN_MOVED_PAIRS} < 0 x y / z a / max ?",
    )

    scores = core.std.StackVertical([static, has_detail])

    return _detached(
        core.resize.Point(
            scores,
            format=vs.GRAY8,
            range_in_s="full",
            range_s="full",
            dither_type="none",
        )
    )


def shape(scores: vs.VideoNode, params: Params = DEFAULT_PARAMS) -> vs.VideoNode | None:
    """Turn `measure`'s scores into a mask, or None when there's nothing worth protecting."""
    height = scores.height // 2

    # scores are 0-255, output float so 1 means 1
    def over(plane: vs.VideoNode, threshold: float) -> vs.VideoNode:
        return core.std.Expr(plane, f"x {threshold * 255} >= 1 0 ?", format=vs.GRAYS)

    static = over(core.std.Crop(scores, bottom=height), params.stillness)
    drawn = over(core.std.Crop(scores, top=height), MIN_DETAIL)

    # sizes below are in analysis pixels
    scale = height / 1080

    def scaled(pixels_at_1080p: int) -> int:
        # at least a pixel unless it's 0
        if pixels_at_1080p <= 0:
            return 0

        return max(1, round(pixels_at_1080p * scale))

    # static pixels need detail, or detail within reach (e.g. the middle of a solid panel)
    reach = scaled(params.fill)
    if reach > 0:
        near_drawn = core.std.BoxBlur(core.std.Expr([static, drawn], "x y min"), hradius=reach, vradius=reach)
        static = core.std.Expr([static, near_drawn], "y 0 > x 0 ?")
    else:
        static = core.std.Expr([static, drawn], "x y min")

    # drop lone static pixels
    density = core.std.BoxBlur(static, hradius=CLUSTER_RADIUS, vradius=CLUSTER_RADIUS)
    static = core.std.Expr([static, density], f"y {CLUSTER_COVERAGE} < 0 x ?")

    fraction = core.std.PlaneStats(static).get_frame(0).props["PlaneStatsAverage"]
    found = fraction * static.width * static.height

    if found < max(1, round(MIN_STATIC_PX_1080P * scale * scale)):
        log.info(f"Mask: nothing static found ({found:.0f} pixels). Rendering unmasked")
        return None

    if fraction > MAX_STATIC_FRACTION:
        log.info(f"Mask: {fraction:.1%} of the frame is static, too much to be an overlay. Rendering unmasked")
        return None

    log.info(f"Mask: protecting the {fraction:.2%} of the frame that never moves")

    padding, feather = scaled(params.padding), scaled(params.feather)

    # grown a ring at a time, with the outer `feather` rings fading out. the fade stays inside the padding and
    # never eats into what was detected, since a tight mask's edge is the overlay's own edge
    fade = min(feather, padding)
    grown = static
    for ring in range(1, padding + 1):
        grown = core.std.Maximum(grown)
        strength = min(1, (padding - ring + 1) / (fade + 1))
        static = core.std.Expr([static, grown], f"x y {strength} * max")

    # white means interpolate as normal
    return _detached(core.std.Expr(static, "1 x -"))


def generate(clip: vs.VideoNode, params: Params = DEFAULT_PARAMS) -> vs.VideoNode | None:
    """`measure` and `shape` in one go, without caching."""
    scores = measure(clip, params.samples)

    return None if scores is None else shape(scores, params)


# Caching
#
# the finished mask is cached per settings, and the scores `measure` works out are cached per sample count so
# changing any of the other settings doesn't need the video to be read again


def _cache_key(video_path: Path, analysed: tuple[int, int], subject: str) -> str:
    """`subject` is the settings the cached file depends on. This file's source is in the key too so changes to
    the analysis don't reuse old results."""
    stat = video_path.stat()
    identity = (f"{video_path.resolve()}\n{stat.st_size}\n{stat.st_mtime_ns}\n{analysed}\n{subject}").encode()

    return hashlib.sha1(identity + Path(__file__).read_bytes()).hexdigest()[:16]


def _cache_name(video_path: Path, key: str) -> str:
    readable = "".join(c if c.isalnum() or c in "-_" else "_" for c in video_path.stem)[:48]

    return f"{readable}-{key}"


def _greyscale_png(plane, width: int, height: int) -> bytes:
    """Hand rolled since none of the plugins blur ships can write images."""
    packed = plane.tobytes()  # drops the stride padding
    scanlines = b"".join(b"\x00" + packed[y * width : (y + 1) * width] for y in range(height))

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    return b"".join(
        [
            b"\x89PNG\r\n\x1a\n",
            # 8 bits a sample, colour type 0 (greyscale), no interlacing
            chunk(b"IHDR", struct.pack(">2I5B", width, height, 8, 0, 0, 0, 0)),
            chunk(b"IDAT", zlib.compress(scanlines, 9)),
            chunk(b"IEND", b""),
        ]
    )


def _store(gray: vs.VideoNode | None, path: Path):
    """Write a one frame GRAY clip out as a png, or note that there wasn't one worth having."""
    if gray is None:
        path.with_suffix(NOTHING_SUFFIX).write_bytes(b"")
        return

    eight_bit = core.resize.Point(gray, format=vs.GRAY8, range_in_s="full", range_s="full", dither_type="none")
    png = _greyscale_png(eight_bit.get_frame(0)[0], eight_bit.width, eight_bit.height)

    # written then moved so half written files are never used. pid since a render and a preview can run at once
    partial = path.with_suffix(f".{os.getpid()}.partial")

    try:
        partial.write_bytes(png)
        partial.replace(path.with_suffix(MASK_SUFFIX))
    except OSError:
        partial.unlink(missing_ok=True)
        raise


def _prune(folder: Path, limit: int):
    """Drop the oldest files in a cache folder once there are more than `limit` of them."""

    # another blur can be pruning the same folder
    def modified(path: Path) -> float:
        try:
            return path.stat().st_mtime
        except OSError:
            return 0.0

    kept = sorted(
        (p for p in folder.iterdir() if p.suffix in (MASK_SUFFIX, NOTHING_SUFFIX)),
        key=modified,
        reverse=True,
    )

    for stale in kept[limit:]:
        try:
            stale.unlink(missing_ok=True)
        except OSError:
            pass  # in use, it'll go next time


def _cached_path(folder: Path, video_path: Path, analysed: tuple[int, int], subject: str) -> Path:
    """Where a video's entry in a cache folder goes, folder made if it wasn't there."""
    folder.mkdir(parents=True, exist_ok=True)

    return folder / _cache_name(video_path, _cache_key(video_path, analysed, subject))


def _scores(
    clip: vs.VideoNode,
    video_path: Path,
    folder: Path,
    analysed: tuple[int, int],
    samples: int,
) -> vs.VideoNode | None:
    """`measure`, cached."""
    try:
        path = _cached_path(folder, video_path, analysed, f"samples={samples}")
    except OSError as e:
        log.info(f"Mask: can't use the cache ({e}), analysing this video every time")
        return measure(clip, samples)

    if path.with_suffix(NOTHING_SUFFIX).exists():
        log.info("Mask: cached as nothing to measure")
        return None

    score_file = path.with_suffix(MASK_SUFFIX)
    if score_file.exists():
        try:
            log.info(f"Mask: reusing measurements from {score_file.name}")

            # read now so a bad file is caught here and nothing holds it open
            return _detached(load(score_file))
        except Exception as e:  # noqa: BLE001
            log.info(f"Mask: couldn't read {score_file.name} ({e}), measuring again")

    scores = measure(clip, samples)

    try:
        _store(scores, path)
        _prune(folder, SCORE_CACHE_LIMIT)
    except OSError as e:
        log.info(f"Mask: couldn't save the measurements ({e})")

    return scores


def cached(
    clip: vs.VideoNode,
    video_path: Path,
    folder: Path,
    score_folder: Path,
    analysed: tuple[int, int],
    params: Params = DEFAULT_PARAMS,
) -> vs.VideoNode | None:
    """`generate`, cached. `analysed` is the frame range the clip covers, since a different trim can find a
    different mask."""
    try:
        path = _cached_path(folder, video_path, analysed, str(params))
    except OSError as e:
        log.info(f"Mask: can't use the cache ({e}), analysing this video every time")
        return generate(clip, params)

    if path.with_suffix(NOTHING_SUFFIX).exists():
        log.info("Mask: cached as nothing worth masking")
        return None

    mask_file = path.with_suffix(MASK_SUFFIX)
    if mask_file.exists():
        try:
            log.info(f"Mask: reusing {mask_file.name}")

            return _detached(load(mask_file))
        except Exception as e:  # noqa: BLE001
            log.info(f"Mask: couldn't read {mask_file.name} ({e}), analysing again")

    # scores are cached separately, so only the shaping has to be redone
    scores = _scores(clip, video_path, score_folder, analysed, params.samples)
    gray = None if scores is None else shape(scores, params)

    try:
        _store(gray, path)
        _prune(folder, CACHE_LIMIT)
    except OSError as e:
        log.info(f"Mask: couldn't save to the cache ({e})")

    return gray


def match(gray: vs.VideoNode, clip: vs.VideoNode) -> vs.VideoNode:
    """Scale a GRAY mask into `clip`'s exact format, since MaskedMerge with a grey mask on subsampled clips is
    fiddly."""
    fmt = clip.format

    luma_format = core.query_video_format(vs.GRAY, fmt.sample_type, fmt.bits_per_sample, 0, 0)

    def resized(width: int, height: int) -> vs.VideoNode:
        # full range both sides, these are coverage values not video levels
        return core.resize.Bilinear(
            gray,
            width=width,
            height=height,
            format=luma_format.id,
            range_in_s="full",
            range_s="full",
        )

    luma = resized(clip.width, clip.height)

    if fmt.num_planes == 1:
        return luma

    chroma = resized(clip.width >> fmt.subsampling_w, clip.height >> fmt.subsampling_h)

    return core.std.ShufflePlanes([luma, chroma, chroma], planes=[0, 0, 0], colorfamily=fmt.color_family)


def match_length(src: vs.VideoNode, target: vs.VideoNode) -> vs.VideoNode:
    """Stretch `src` over `target`'s frame count by repeating frames. Not using change_fps since it needs an
    integer fps."""
    if src.format.id != target.format.id or src.width != target.width or src.height != target.height:
        src = core.resize.Bicubic(src, width=target.width, height=target.height, format=target.format.id)

    if src.num_frames == target.num_frames:
        return src

    if src.num_frames == 0:
        raise u.BlurException("Can't apply a mask to an empty clip")

    factor = target.num_frames / src.num_frames
    last = src.num_frames - 1

    return core.std.FrameEval(core.std.BlankClip(target), lambda n: src[min(round(n / factor), last)])


def combine(grays: list[vs.VideoNode]) -> vs.VideoNode:
    """A pixel is protected if any of the masks protects it. They're scaled up to the largest one first."""
    if len(grays) == 1:
        return grays[0]

    width = max(gray.width for gray in grays)
    height = max(gray.height for gray in grays)

    def sized(gray: vs.VideoNode) -> vs.VideoNode:
        # full range both sides, these are coverage values not video levels
        return core.resize.Bilinear(
            gray,
            width=width,
            height=height,
            format=vs.GRAYS,
            range_in_s="full",
            range_s="full",
        )

    combined = sized(grays[0])
    for gray in grays[1:]:
        # black protects, so take the darker one
        combined = core.std.Expr([combined, sized(gray)], "x y min")

    return combined


# the fill's coarsest level is around this tall
FILL_MIN_HEIGHT = 8


def fill(clip: vs.VideoNode, gray: vs.VideoNode) -> vs.VideoNode:
    """Paint over anything the mask protects with the scene around it. Otherwise interpolation drags overlays
    along with the scene, and the copies land just outside the mask."""
    fmt = clip.format
    working = core.query_video_format(fmt.color_family, vs.FLOAT, 32, fmt.subsampling_w, fmt.subsampling_h)

    # only pixels the mask leaves completely alone count as scene
    white = (1 << fmt.bits_per_sample) - 1 if fmt.sample_type == vs.INTEGER else 1
    scene = core.std.Expr(match(gray, clip), f"x {white * 0.999} >= 1 0 ?", format=working.id)

    # push-pull: halve the scene (weighted by `scene`) until the holes are gone, then fill each level's holes
    # from the one below it
    colour = core.resize.Point(clip, format=working.id)
    levels = [(core.std.Expr([colour, scene], "x y *"), scene)]

    width_step, height_step = 1 << fmt.subsampling_w, 1 << fmt.subsampling_h
    width, height = clip.width, clip.height
    while height > FILL_MIN_HEIGHT:
        width = max(width_step, width // 2 // width_step * width_step)
        height = max(height_step, height // 2 // height_step * height_step)

        premultiplied, weight = levels[-1]
        levels.append(
            (
                core.resize.Bilinear(premultiplied, width=width, height=height),
                core.resize.Bilinear(weight, width=width, height=height),
            )
        )

    premultiplied, weight = levels[-1]
    filled = core.std.Expr([premultiplied, weight], "y 0.000001 > x y / 0 ?")

    for premultiplied, weight in reversed(levels[:-1]):
        below = core.resize.Bilinear(filled, width=premultiplied.width, height=premultiplied.height)
        filled = core.std.Expr([premultiplied, weight, below], "x 1 y - z * +")

    filled = core.resize.Point(filled, format=fmt.id, dither_type="none")

    # scene pixels go back exactly as they were
    return core.std.Expr([clip, filled, scene], "z 0 > x y ?")


def protect(interpolated: vs.VideoNode, original: vs.VideoNode, gray: vs.VideoNode) -> vs.VideoNode:
    """Put `original`'s pixels back wherever the mask is black."""
    base = match_length(original, interpolated)

    # white takes clipb (interpolated), black takes clipa (original)
    return core.std.MaskedMerge(base, interpolated, match(gray, interpolated))


def preview(clip: vs.VideoNode, grays: list[vs.VideoNode]) -> vs.VideoNode:
    """The mask on its own at the render's size, for the config screen's mask preview."""
    if grays:
        gray = combine(grays)
    else:
        gray = core.std.BlankClip(clip, format=vs.GRAYS, color=1.0)

    # masks read from pngs say their matrix is rgb, which a grey clip can't be converted to yuv with.
    # 2 is unspecified
    gray = core.std.SetFrameProps(gray, _Matrix=2)

    shown = core.resize.Bilinear(
        gray,
        width=clip.width,
        height=clip.height,
        format=clip.format.id,
        # full range both sides, these are coverage values not video levels
        range_in_s="full",
        range_s="full",
    )

    return match_length(shown, clip)
