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
from collections import deque
from collections.abc import Callable
from dataclasses import dataclass, replace
from fractions import Fraction
from pathlib import Path

import numpy as np

import blur.utils as u
import vapoursynth as vs
from blur import log, retime
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
# with tracking on, `track` adds whatever holds still in each frame on top, for overlays that come and go (kill
# feeds, ammo counts, scoreboards) and so never make it into the whole clip mask
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

# more of the frame than this being static means it's not a hud (e.g. a locked off shot), so no whole clip mask
# is made
MAX_STATIC_FRACTION = 0.5

# with `enclosed` on, areas the static mask fully surrounds are protected too, up to this much of the frame each.
# those are the insides of translucent panels, which change with the scene behind them so are never static themselves,
# and otherwise get dragged out past their borders by interpolation
MAX_ENCLOSED_FRACTION = 0.03

# looked for at this fraction of the analysis size, panels are big enough that the detail doesn't matter
ENCLOSED_SCALE = 4

# `track`: a pixel holds still in a frame when it changes by less than STILL_THRESHOLD, or STILL_RATIO of how
# much the area around it changes (the median, radius at 1080p) if that's more. compression gets noisier as the
# scene moves, so static text can change by a few levels during a flick
STILL_THRESHOLD = 0.012
STILL_RATIO = 0.15
STILL_AREA_RADIUS_PX_1080P = 24

# it has to hold still for this long on at least one side of the frame, slow scenery can stay under the
# threshold for a frame or two
STILL_SECONDS = 0.05

# and in most frames this far either side, so the mask doesn't flicker
STILL_VOTE_SECONDS = 0.04

# the most memory the source's frames are kept in for tracking to read back
SOURCE_CACHE_BYTES = 1 << 30

# still pixels are dropped unless this much of the box around them is still too
STILL_CLUSTER_COVERAGE = 0.25

# more of a frame's detail holding still than this means the camera is, and then nothing moving is there to tell
# overlays apart from the scene
MAX_STILL_DETAIL = 0.25

# `estimate_layer`: how far past the whole clip mask (at 1080p) how much of the scene shows through is worked out,
# for overlays' soft edges. further out the estimate is too noisy, and leaves a haze over the scene
LAYER_GROW_PX_1080P = 2

# the estimate is corrected for the inpainted scene behind being a rough guess where the two line up this well, and
# trusted as is where they don't (an overlay that changes, like a timer, doesn't follow the scene at all)
LAYER_MIN_CORRELATION = 0.5

# past the mask itself, this much of the scene showing through is taken to be all of it
LAYER_SNAP = 0.6

# where less of the scene than LAYER_UNMIX_FROM shows through it's inpainted rather than worked back out, which
# would amplify noise. from LAYER_UNMIX_FULL it's worked out entirely
LAYER_UNMIX_FROM = 0.3
LAYER_UNMIX_FULL = 0.6

LAYER_SUFFIX = ".npz"


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
    enclosed: bool = True
    enclosed_max_fps: int = 180

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
            enclosed=settings.get("auto_mask_enclosed") is not False,
            enclosed_max_fps=number("auto_mask_enclosed_max_fps", 180, 1, 100000),
        )

    def for_clip(self, clip: vs.VideoNode) -> "Params":
        """The settings as they apply to `clip`. Enclosed areas aren't masked above `enclosed_max_fps`, where
        interpolation barely smears them."""
        fps = clip.fps_num / clip.fps_den if clip.fps_num else 0
        return replace(self, enclosed=self.enclosed and fps <= self.enclosed_max_fps)


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


def _detail_expression(threshold: float) -> str:
    """akarin expression for whether pixel x differs from its local median y, or sits on an edge. A clean edge is
    its own median, so the spread of the 3x3 around it is checked too."""
    around = [f"x[{dx},{dy}]" for dy in (-1, 0, 1) for dx in (-1, 0, 1)]
    highest = " ".join(around) + " max" * (len(around) - 1)
    lowest = " ".join(around) + " min" * (len(around) - 1)

    return f"x y - abs {threshold} > {highest} {lowest} - {threshold} > or"


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
        detail = core.akarin.Expr([sample, local], f"{_detail_expression(LOCAL_CONTRAST)} 1 0 ?")
        either = core.std.Expr([either, detail], "x y +")
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
        log.info(f"Mask: only {len(indices)} frames differ from each other, not enough to find a whole clip mask")
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
        # half of one pixel's share of the box, since the blur's running sum leaves tiny values where it should be 0
        static = core.std.Expr([static, near_drawn], f"y {0.5 / (2 * reach + 1) ** 2} > x 0 ?")
    else:
        static = core.std.Expr([static, drawn], "x y min")

    # drop lone static pixels
    density = core.std.BoxBlur(static, hradius=CLUSTER_RADIUS, vradius=CLUSTER_RADIUS)
    static = core.std.Expr([static, density], f"y {CLUSTER_COVERAGE} < 0 x ?")

    if params.enclosed:
        static = _with_enclosed(static)

    fraction = core.std.PlaneStats(static).get_frame(0).props["PlaneStatsAverage"]
    found = fraction * static.width * static.height

    if found < max(1, round(MIN_STATIC_PX_1080P * scale * scale)):
        log.info(f"Mask: nothing static across the whole clip ({found:.0f} pixels)")
        return None

    if fraction > MAX_STATIC_FRACTION:
        log.info(
            f"Mask: {fraction:.1%} of the frame is static, too much to be an overlay. Leaving the whole clip mask out"
        )
        return None

    log.info(f"Mask: protecting the {fraction:.2%} of the frame that never moves")

    static = _grown(static, scaled(params.padding), scaled(params.feather))

    # white means interpolate as normal
    return _detached(core.std.Expr(static, "1 x -"))


def _enclosed(protected: np.ndarray) -> np.ndarray:
    """Where `protected` fully surrounds an area no bigger than MAX_ENCLOSED_FRACTION of the frame."""
    height, width = protected.shape
    limit = MAX_ENCLOSED_FRACTION * height * width
    seen = protected.copy()
    found = np.zeros_like(protected)

    def flood(starts: list[tuple[int, int]]) -> list[tuple[int, int]]:
        area = [start for start in starts if not seen[start]]
        for start in area:
            seen[start] = True

        queue = deque(area)
        while queue:
            y, x = queue.popleft()
            for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if 0 <= ny < height and 0 <= nx < width and not seen[ny, nx]:
                    seen[ny, nx] = True
                    area.append((ny, nx))
                    queue.append((ny, nx))

        return area

    # everything the frame's edge can reach isn't enclosed
    edge = [(y, x) for y in range(height) for x in (0, width - 1)]
    edge += [(y, x) for x in range(width) for y in (0, height - 1)]
    flood(edge)

    for start in zip(*np.nonzero(~seen)):
        if seen[start]:
            continue

        area = flood([start])
        if len(area) <= limit:
            ys, xs = zip(*area)
            found[list(ys), list(xs)] = True

    return found


def _with_enclosed(static: vs.VideoNode) -> vs.VideoNode:
    """`static` (1 where protected) with what it encloses protected too."""
    small = core.resize.Bilinear(
        static, width=max(1, static.width // ENCLOSED_SCALE), height=max(1, static.height // ENCLOSED_SCALE)
    )
    enclosed = _enclosed(np.asarray(small.get_frame(0)[0]) >= 0.5)
    if not enclosed.any():
        return static

    def add(n: int, f: vs.VideoFrame) -> vs.VideoFrame:
        out = f.copy()
        plane = np.asarray(out[0])

        # nearest neighbour back up to size, the edges are inside the static mask anyway
        ys = np.minimum(np.arange(static.height) // ENCLOSED_SCALE, enclosed.shape[0] - 1)
        xs = np.minimum(np.arange(static.width) // ENCLOSED_SCALE, enclosed.shape[1] - 1)
        plane[enclosed[ys][:, xs]] = 1.0

        return out

    return core.std.ModifyFrame(static, static, add)


def _grown(found: vs.VideoNode, padding: int, feather: int) -> vs.VideoNode:
    """`found` (1 where protected) grown a ring at a time, with the outer `feather` rings fading out. The fade stays
    inside the padding and never eats into what was found, since a tight mask's edge is the overlay's own edge."""
    fade = min(feather, padding)
    grown = found
    for ring in range(1, padding + 1):
        grown = core.std.Maximum(grown)
        strength = min(1, (padding - ring + 1) / (fade + 1))
        found = core.std.Expr([found, grown], f"x y {strength} * max")

    return found


def generate(clip: vs.VideoNode, params: Params = DEFAULT_PARAMS) -> vs.VideoNode | None:
    """`measure` and `shape` in one go, without caching."""
    scores = measure(clip, params.samples)

    return None if scores is None else shape(scores, params)


# Layers
#
# putting the original back over what the mask covers is right for an opaque overlay, but not for one the scene
# shows through, or for an overlay's soft edges: those show the scene from a frame that's no longer current. so
# what the whole clip mask covers is treated as a layer over the scene, `observed = a + b * scene` at each pixel,
# with b how much of the scene shows through (0 where the overlay is opaque, 1 where there isn't one). b and a are
# worked out once per clip, by comparing what's observed against the scene inpainted from around the mask.
# `separate` takes the layer out before interpolating and `composite` puts it back over the interpolated scene,
# keeping the original's overlay (so one that changes, like a timer, stays right) and moving what shows through


@dataclass(frozen=True)
class Layer:
    """One frame clips at the analysis size: `a` in float yuv (or gray), `b` GRAYS."""

    a: vs.VideoNode
    b: vs.VideoNode


def _planes(frame: vs.VideoFrame) -> np.ndarray:
    return np.stack([np.asarray(frame[i]) for i in range(frame.format.num_planes)]).astype(np.float64)


def _dilated(found: np.ndarray, steps: int) -> np.ndarray:
    for _ in range(steps):
        grown = found.copy()
        grown[1:] |= found[:-1]
        grown[:-1] |= found[1:]
        grown[:, 1:] |= found[:, :-1]
        grown[:, :-1] |= found[:, 1:]
        found = grown

    return found


def _constant(planes: np.ndarray) -> vs.VideoNode:
    """A one frame float clip holding `planes` (planes, height, width): GRAYS for one, YUV444PS for three."""
    data = planes.astype(np.float32)
    fmt = vs.GRAYS if data.shape[0] == 1 else vs.YUV444PS
    holder = core.std.BlankClip(width=data.shape[2], height=data.shape[1], format=fmt, length=1, keep=True)

    def put(n: int, f: vs.VideoFrame) -> vs.VideoFrame:
        out = f.copy()
        for index in range(data.shape[0]):
            np.copyto(np.asarray(out[index]), data[index])

        return out

    return core.std.ModifyFrame(holder, holder, put)


def _layer_arrays(clip: vs.VideoNode, static: vs.VideoNode, samples: int) -> tuple[np.ndarray, np.ndarray]:
    """`estimate_layer`'s a and b, as arrays."""
    analysed = _analysed(clip)
    width, height = analysed.width, analysed.height

    resized = core.resize.Bilinear(static, width=width, height=height, format=vs.GRAYS)
    found = 1 - np.asarray(resized.get_frame(0)[0]) > 0.01
    region = _dilated(found, max(1, round(LAYER_GROW_PX_1080P * height / 1080)))

    # white is the scene the inpainting works from
    around = _constant(np.where(region, 0.0, 1.0)[None])

    observed_sum = behind_sum = product_sum = behind_squares = observed_squares = 0
    indices = _samples(analysed, samples)
    for index in indices:
        frame = analysed[index]
        observed = _planes(frame.get_frame(0))
        behind = _planes(fill(frame, around).get_frame(0))

        observed_sum = observed_sum + observed
        behind_sum = behind_sum + behind
        product_sum = product_sum + observed * behind
        behind_squares = behind_squares + behind * behind
        observed_squares = observed_squares + observed * observed

    count = max(1, len(indices))
    observed_mean, behind_mean = observed_sum / count, behind_sum / count
    covariance = product_sum / count - observed_mean * behind_mean
    behind_variance = behind_squares / count - behind_mean**2
    observed_variance = observed_squares / count - observed_mean**2

    # from brightness, which varies the most behind an overlay. regressing observed on the inpainted guess
    # understates b since the guess is rough. regressing the other way round doesn't, where the two line up
    luma_covariance, luma_behind, luma_observed = covariance[0], behind_variance[0], observed_variance[0]
    b = np.where(luma_behind > 1e-4, luma_covariance / np.maximum(luma_behind, 1e-4), 0.0)
    correlation = luma_covariance / np.sqrt(np.maximum(luma_observed * luma_behind, 1e-12))
    reversed_b = np.where(luma_covariance > 1e-6, luma_observed / np.maximum(luma_covariance, 1e-6), 0.0)
    b = np.where((correlation > LAYER_MIN_CORRELATION) & (luma_observed > 1e-5), reversed_b, b)

    b = np.clip(b, 0, 1)
    b = np.where(~found & (b > LAYER_SNAP), 1.0, b)
    b = np.where(region, b, 1.0)

    a = np.where(region[None], observed_mean - b[None] * behind_mean, 0.0)

    return a, b


def estimate_layer(clip: vs.VideoNode, static: vs.VideoNode, samples: int = SAMPLE_COUNT) -> Layer:
    """What the whole clip mask `static` covers, as a layer over the scene."""
    a, b = _layer_arrays(clip, static, samples)

    return Layer(_constant(a), _constant(b[None]))


def _tracked(clip: vs.VideoNode) -> vs.VideoNode:
    """`clip` ready for `track`: 4:4:4 at its own bit depth (so 8 bit stays cheap), no taller than
    MAX_ANALYSIS_HEIGHT."""
    fmt = clip.format
    bits = 8 if fmt.sample_type == vs.INTEGER and fmt.bits_per_sample <= 8 else 16
    family = vs.GRAY if fmt.color_family == vs.GRAY else vs.YUV
    convert = {"format": core.query_video_format(family, vs.INTEGER, bits, 0, 0).id}

    if fmt.color_family == vs.RGB:
        # any matrix works, the values are only compared against each other
        convert["matrix_s"] = "709"

    if clip.height <= MAX_ANALYSIS_HEIGHT:
        return core.resize.Point(clip, **convert)

    return core.resize.Bilinear(
        clip,
        width=round(clip.width * MAX_ANALYSIS_HEIGHT / clip.height / 2) * 2,
        height=MAX_ANALYSIS_HEIGHT,
        **convert,
    )


def _shifted(clip: vs.VideoNode, offset: int) -> vs.VideoNode:
    """`clip` moved `offset` frames earlier (later when negative), holding the end frame to keep the length."""
    if offset > 0:
        return clip[offset:] + clip[-1] * offset

    return clip[0] * -offset + clip[:offset]


def track(clip: vs.VideoNode, static: vs.VideoNode | None, params: Params = DEFAULT_PARAMS) -> vs.VideoNode:
    """`static` plus whatever has detail and holds still in each frame of `clip`, as a mask with one frame for
    each of `clip`'s."""
    analysed = _tracked(clip)
    peak = (1 << analysed.format.bits_per_sample) - 1
    scale = analysed.height / 1080
    fps = analysed.fps_num / analysed.fps_den if analysed.fps_num else 60

    padding = max(1, round(params.padding * scale)) if params.padding > 0 else 0
    feather = max(1, round(params.feather * scale)) if params.feather > 0 else 0

    if analysed.num_frames < 2:
        return static if static is not None else core.std.BlankClip(analysed, format=vs.GRAYS, color=1.0)

    # compared against frames this far away on each side, the ends of the span and the middle
    span = min(max(1, round(STILL_SECONDS * fps)), analysed.num_frames - 1)
    vote = round(STILL_VOTE_SECONDS * fps)

    # vapoursynth's automatic caching drops frames before the vote's window is done with them, and long gop
    # sources seek back and decode again for each one
    reach = span + vote
    chroma = (clip.format.num_planes - 1) / (1 << (clip.format.subsampling_w + clip.format.subsampling_h))
    frame_bytes = int(clip.width * clip.height * clip.format.bytes_per_sample * (1 + chroma))
    core.std.SetVideoCache(
        clip, mode=1, fixedsize=1, maxsize=min(4 * reach, max(2 * reach, SOURCE_CACHE_BYTES // frame_bytes))
    )
    offsets = sorted({1, max(1, span // 2), span})

    # how much the area around each pixel changes over the span, in brightness
    brightness = _plane(analysed, 0)
    area_radius = max(1, round(STILL_AREA_RADIUS_PX_1080P * scale / 8))

    def area_change(offset: int) -> vs.VideoNode:
        change = core.std.Expr([brightness, _plane(_shifted(analysed, offset), 0)], "x y - abs")

        # the median at an eighth of the size, so one moving edge doesn't raise it for everything near it
        small = core.resize.Bilinear(change, width=max(8, analysed.width // 8), height=max(8, analysed.height // 8))
        return core.resize.Bilinear(_local_median(small, area_radius), width=analysed.width, height=analysed.height)

    area = core.std.Expr([area_change(span), area_change(-span)], "x y max")

    # one expression for the whole test, since every clip here holds frames from across the span
    later = [_shifted(analysed, offset) for offset in offsets]
    earlier = [_shifted(analysed, -offset) for offset in offsets]
    clips = [analysed, *later, *earlier, _every_plane(area, analysed)]
    threshold = f"{STILL_THRESHOLD * peak} src{len(clips) - 1} {STILL_RATIO} * max"

    def held(side: range) -> str:
        changes = " ".join(f"src{i} src0 - abs" for i in side) + " max" * (len(side) - 1)
        return f"{changes} {threshold} <="

    after = range(1, 1 + len(offsets))
    before = range(1 + len(offsets), 1 + 2 * len(offsets))

    # still for the span on either side, and against both neighbours
    neighbours = f"{held(range(after.start, after.start + 1))} {held(range(before.start, before.start + 1))} and"
    expression = f"{held(after)} {held(before)} or {neighbours} and {peak} 0 ?"
    per_plane = core.akarin.Expr(clips, expression)

    # every plane has to hold still
    still = _plane(per_plane, 0)
    for index in range(1, analysed.format.num_planes):
        still = core.std.Expr([still, _plane(per_plane, index)], "x y min")

    radius = max(1, round(LOCAL_RADIUS_PX_1080P * scale))
    local = _local_median(analysed, radius)
    # just the median here, counting edges lets in scenery that compression left untouched between frames
    detail = _best_plane(core.akarin.Expr([analysed, local], f"x y - abs {LOCAL_CONTRAST * peak} > {peak} 0 ?"))

    found = core.std.Expr([still, detail], "x y min")
    density = core.std.BoxBlur(found, hradius=CLUSTER_RADIUS, vradius=CLUSTER_RADIUS)
    found = core.std.Expr([found, density], f"y {STILL_CLUSTER_COVERAGE * peak} < 0 x ?")

    # nothing is moving to tell overlays from the scene when most detail holds still
    found = core.std.PlaneStats(found, prop="Found")
    detail = core.std.PlaneStats(detail, prop="Detail")
    found = core.akarin.Expr(
        [found, detail],
        f"x.FoundAverage y.DetailAverage 0.000001 + / {MAX_STILL_DETAIL} > 0 x ?",
    )

    if vote > 0:
        found = core.frameblender.FrameBlend(found, weights=[1] * (2 * vote + 1))

    # grown like _grown, but counting the fade in whole steps so it stays at the analysis' depth until the end
    steps = min(feather, padding) + 1
    protected = core.std.Expr(found, f"x {peak / 2} >= {steps} 0 ?")
    grown = protected
    for ring in range(1, padding + 1):
        grown = core.std.Maximum(grown)
        protected = core.std.Expr([protected, grown], f"x y 0 > {min(steps, padding - ring + 1)} 0 ? max")

    # without the analysis' yuv matrix, which stops it being resized into an rgb clip
    gray = core.std.RemoveFrameProps(core.std.Expr(protected, f"1 x {steps} / -", format=vs.GRAYS), "_Matrix")

    if static is None:
        return gray

    static = core.resize.Bilinear(static, width=gray.width, height=gray.height, format=vs.GRAYS)

    # black protects, so take the darker one
    return core.std.Expr([gray, static], "x y min")


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
        (p for p in folder.iterdir() if p.suffix in (MASK_SUFFIX, NOTHING_SUFFIX, LAYER_SUFFIX)),
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


def cached_layer(
    clip: vs.VideoNode,
    static: vs.VideoNode,
    video_path: Path,
    folder: Path,
    analysed: tuple[int, int],
    params: Params = DEFAULT_PARAMS,
) -> Layer:
    """`estimate_layer`, cached alongside the measurements."""
    try:
        path = _cached_path(folder, video_path, analysed, f"layer {params}").with_suffix(LAYER_SUFFIX)
    except OSError as e:
        log.info(f"Mask: can't use the cache ({e}), working out the layer every time")
        return estimate_layer(clip, static, params.samples)

    if path.exists():
        try:
            with np.load(path) as stored:
                return Layer(_constant(stored["a"]), _constant(stored["b"][None]))
        except Exception as e:  # noqa: BLE001
            log.info(f"Mask: couldn't read {path.name} ({e}), working the layer out again")

    a, b = _layer_arrays(clip, static, params.samples)

    # written then moved so half written files are never used. pid since a render and a preview can run at once
    partial = path.with_suffix(f".{os.getpid()}.partial")
    try:
        with partial.open("wb") as f:
            np.savez(f, a=a.astype(np.float32), b=b.astype(np.float32))
        partial.replace(path)
        _prune(folder, SCORE_CACHE_LIMIT)
    except OSError as e:
        partial.unlink(missing_ok=True)
        log.info(f"Mask: couldn't save the layer ({e})")

    return Layer(_constant(a), _constant(b[None]))


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
    frame = retime.single_frames(src)

    return core.std.FrameEval(core.std.BlankClip(target), lambda n: frame(round(n / factor)))


def combine(grays: list[vs.VideoNode]) -> vs.VideoNode:
    """A pixel is protected if any of the masks protects it. They're scaled up to the largest one first."""
    if len(grays) == 1:
        return grays[0]

    # expressions take their length from the first clip, so a one frame mask can't cut a tracked one short
    grays = sorted(grays, key=lambda gray: gray.num_frames, reverse=True)

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
    white = (1 << fmt.bits_per_sample) - 1 if fmt.sample_type == vs.INTEGER else 1
    plane_format = core.query_video_format(vs.GRAY, fmt.sample_type, fmt.bits_per_sample, 0, 0).id

    # only pixels the mask leaves completely alone count as scene
    scene = core.std.Expr(match(gray, clip), f"x {white * 0.999} >= {white} 0 ?")

    # push-pull: halve the scene (weighted by `scene`) until the holes are gone, then fill each level's holes
    # from the one below it. full size only ever holds the scene or the fill, so it stays at the clip's own depth
    # and the levels start at half size
    width_step, height_step = 1 << fmt.subsampling_w, 1 << fmt.subsampling_h
    width, height = clip.width, clip.height
    sizes = []
    while height > FILL_MIN_HEIGHT or not sizes:
        width = max(width_step, width // 2 // width_step * width_step)
        height = max(height_step, height // 2 // height_step * height_step)
        sizes.append((width, height))

    # each plane on its own, so integer values convert to float without the yuv offsets (which would stop 0
    # meaning no scene). planes the same size share their weights
    weights = {}
    planes = []
    for index in range(fmt.num_planes):
        plane, weight = _plane(clip, index), _plane(scene, index)
        shift_w, shift_h = (fmt.subsampling_w, fmt.subsampling_h) if index else (0, 0)
        level_sizes = [(w >> shift_w, h >> shift_h) for w, h in sizes]

        if (plane.width, plane.height) not in weights:
            weight_levels = [_to_float(weight, *level_sizes[0])]
            for size in level_sizes[1:]:
                weight_levels.append(core.resize.Bilinear(weight_levels[-1], *size))
            weights[plane.width, plane.height] = weight_levels

        weight_levels = weights[plane.width, plane.height]

        premultiplied = [_to_float(core.std.Expr([plane, weight], "y 0 > x 0 ?"), *level_sizes[0])]
        for size in level_sizes[1:]:
            premultiplied.append(core.resize.Bilinear(premultiplied[-1], *size))

        filled = core.std.Expr([premultiplied[-1], weight_levels[-1]], "y 0.000001 > x y / 0 ?")
        for level, level_weight in reversed(list(zip(premultiplied[:-1], weight_levels[:-1]))):
            below = core.resize.Bilinear(filled, width=level.width, height=level.height)
            filled = core.std.Expr([level, level_weight, below], "x 1 y - z * +")

        filled = core.resize.Bilinear(
            _unranged(filled),
            width=plane.width,
            height=plane.height,
            format=plane_format,
            dither_type="none",
            range_in_s="full",
            range_s="full",
        )

        # scene pixels go back exactly as they were
        planes.append(core.std.Expr([plane, filled, weight], "z 0 > x y ?"))

    return core.std.ShufflePlanes(planes, [0] * fmt.num_planes, fmt.color_family)


def _unranged(clip: vs.VideoNode) -> vs.VideoNode:
    """`clip` without a range, which resize would take over the range it's told."""
    return core.std.RemoveFrameProps(clip, ["_Range", "_ColorRange"])


def _float_scale(fmt: vs.VideoFormat, index: int) -> str:
    """akarin expression storing S and O, which take plane `index` of x from float (as resize converts it, going
    by x's range) to `fmt`'s values: value = float * S + O."""
    if fmt.sample_type == vs.FLOAT:
        return "1 S! 0 O!"

    step = 1 << (fmt.bits_per_sample - 8)
    peak = (1 << fmt.bits_per_sample) - 1
    if fmt.color_family == vs.YUV and index > 0:
        full, limited = (peak, 128 * step), (224 * step, 128 * step)
    elif fmt.color_family == vs.RGB:
        full = limited = (peak, 0)
    else:
        full, limited = (peak, 0), (219 * step, 16 * step)

    # a missing range counts as limited, like resize. akarin compares a missing property as equal to anything
    is_limited = "x._Range 0 ="
    return f"{is_limited} {limited[0]} {full[0]} ? S! {is_limited} {limited[1]} {full[1]} ? O!"


def _to_float(plane: vs.VideoNode, width: int, height: int) -> vs.VideoNode:
    """A GRAY plane resized to GRAYS, with integer values mapped straight to 0-1."""
    return core.resize.Bilinear(
        _unranged(plane), width=width, height=height, format=vs.GRAYS, range_in_s="full", range_s="full"
    )


def _per_plane(gray: vs.VideoNode, like: vs.VideoNode) -> vs.VideoNode:
    """A float GRAY clip resized into each plane of `like` (a float format), so expressions can combine the two."""
    fmt = like.format
    luma = core.resize.Bilinear(gray, width=like.width, height=like.height, format=vs.GRAYS)

    if fmt.num_planes == 1:
        return luma

    chroma = core.resize.Bilinear(
        gray,
        width=like.width >> fmt.subsampling_w,
        height=like.height >> fmt.subsampling_h,
        format=vs.GRAYS,
    )
    return core.std.ShufflePlanes([luma, chroma, chroma], planes=[0, 0, 0], colorfamily=fmt.color_family)


def separate(
    clip: vs.VideoNode, gray: vs.VideoNode, layer: Layer | None = None, inpaint: bool = True
) -> tuple[vs.VideoNode, vs.VideoNode]:
    """`clip` with its overlays taken out for interpolating, and how much of the scene shows through at each pixel
    (GRAYS, 1 where there's no overlay) for `composite` to put them back with. Without `inpaint` what can't be
    worked back out of the layer is left as it is."""
    if layer is None:
        return fill(clip, gray) if inpaint else clip, gray

    fmt = clip.format
    working = core.query_video_format(fmt.color_family, vs.FLOAT, 32, fmt.subsampling_w, fmt.subsampling_h)

    b = core.resize.Bilinear(layer.b, width=clip.width, height=clip.height)

    # inpainted where too little of the scene shows through to work it back out
    inpainted = fill(clip, core.std.Expr(b, "x 0.999 >= 1 0 ?")) if inpaint else clip

    a = core.resize.Bilinear(layer.a, width=clip.width, height=clip.height, format=working.id)
    lowest, full = LAYER_UNMIX_FROM, LAYER_UNMIX_FULL
    weight = f"z {lowest} - {full - lowest} / 0 max 1 min"

    # the layer is in float, the clip's values are converted to and from it inside the expression
    expressions = []
    for index in range(fmt.num_planes):
        to_float = _float_scale(fmt, index)
        unmixed = f"X@ y - z {lowest} max / {weight} * a O@ - S@ / 1 {weight} - * +"
        expressions.append(f"{to_float} x O@ - S@ / X! z 0.999 >= X@ {unmixed} ? S@ * O@ +")

    separated = core.akarin.Expr([clip, a, _per_plane(b, a), inpainted], expressions)

    # what the rest of the mask protects more than the layer does (tracking, a mask preset) is inpainted as before
    sized = core.resize.Bilinear(gray, width=clip.width, height=clip.height, format=vs.GRAYS)
    if inpaint:
        separated = fill(separated, core.std.Expr([sized, b], "x y 0.05 - < x 1 ?"))

    return separated, core.std.Expr([sized, b], "x y min")


def composite(
    interpolated: vs.VideoNode,
    original: vs.VideoNode,
    separated: vs.VideoNode,
    through: vs.VideoNode,
    timeline: retime.Timeline | None = None,
    ratio: Fraction | None = None,
) -> vs.VideoNode:
    """Put the overlays `separate` took out back over the interpolated scene: the original's overlay, plus however
    much of the scene shows through moving on since. With the `timeline` `interpolated` was retimed onto (`ratio`
    output frames to each of `original`'s), each output frame goes by the real frame nearest to it."""

    # a tracked mask follows `original`'s frames, a one frame mask is just repeated
    shows = match(through, interpolated)
    tracked = through.num_frames > 1
    real = [original, separated, shows] if tracked else [original, separated]
    real = stretched(real, interpolated, timeline, ratio)

    if tracked:
        shows = real.pop()

    return put_back(interpolated, *real, shows)


def composite_blurred(
    interpolated: vs.VideoNode,
    original: vs.VideoNode,
    separated: vs.VideoNode,
    through: vs.VideoNode,
    timeline: retime.Timeline | None,
    ratio: Fraction | None,
    blend: Callable[[vs.VideoNode], vs.VideoNode],
) -> vs.VideoNode:
    """`blend(composite(...))` for a mask that's the same on every frame, done the other way round. Putting the
    overlays back is a per-pixel mix and blending is a weighted average, so blending what goes into the mix gives the
    same result, and the mix then runs once per output frame instead of on every interpolated one. The real frames are
    repeated across the interpolated ones, which the blender only reads once. With preserve brightness the blend isn't
    linear, so it's slightly off where the mask is partly see-through, like its feathered edge."""
    real = stretched([original] if separated is original else [original, separated], interpolated, timeline, ratio)
    real = [blend(clip) for clip in real]
    blurred = blend(interpolated)
    return put_back(blurred, real[0], real[-1], match(through, blurred))


def stretched(
    clips: list[vs.VideoNode],
    interpolated: vs.VideoNode,
    timeline: retime.Timeline | None = None,
    ratio: Fraction | None = None,
) -> list[vs.VideoNode]:
    """Real frames repeated over `interpolated`'s frames, each going by the real frame nearest to it (see
    `composite`)."""
    if timeline is not None and ratio is not None:
        return retime.nearest(clips, timeline, interpolated, ratio)

    return [match_length(clip, interpolated) for clip in clips]


def put_back(
    interpolated: vs.VideoNode, original: vs.VideoNode, separated: vs.VideoNode, shows: vs.VideoNode
) -> vs.VideoNode:
    """`composite` on clips already lined up frame for frame."""
    fmt = interpolated.format
    white = (1 << fmt.bits_per_sample) - 1 if fmt.sample_type == vs.INTEGER else 1

    # just the interpolated frame where nothing's covered, since separated is the original there
    return core.std.Expr([interpolated, original, separated, shows], f"y a {white} / x z - * +")


def save(grays: list[vs.VideoNode], clip: vs.VideoNode, path: Path):
    """Write one frame masks combined, at `clip`'s size, as a png. All white (masking nothing) when there are
    none."""
    if grays:
        gray = combine(grays)
    else:
        gray = core.std.BlankClip(clip, format=vs.GRAYS, color=1.0, length=1)

    eight_bit = core.resize.Bilinear(
        gray,
        width=clip.width,
        height=clip.height,
        format=vs.GRAY8,
        range_in_s="full",
        range_s="full",
    )
    path.write_bytes(_greyscale_png(eight_bit.get_frame(0)[0], eight_bit.width, eight_bit.height))


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
