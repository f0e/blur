"""Masks protect regions of the frame from interpolation.

The interpolator has no idea a HUD isn't part of the scene, so it warps it along with the world behind it.
A mask marks those regions, and the original frames get put back over them afterwards - so they never get
warped, while still going through frame blending like everything else. Deduplication fills a dropped frame by
interpolating one, so it warps an overlay the same way and a mask covers it too.

A mask is an image in <settings path>/masks: white where the frame should be interpolated as normal, black
where it should be left alone. That's the base mask - the one that's the same for every video, a game's HUD
say. The "auto mask" setting works a second one out from the video itself (see `generate`) and stacks it over
the base, catching whatever that particular video has that the base doesn't cover. Either can be used on its
own; with both, a pixel is protected if either of them protects it - see `combine`.
"""

from vapoursynth import core
import vapoursynth as vs

import hashlib
import struct
import zlib
from pathlib import Path

import blur.utils as u
from blur import log


def load(path: Path) -> vs.VideoNode:
    """Read a mask image into a one frame GRAY clip."""
    if not path.exists():
        raise u.BlurException(f"Mask '{path.name}' wasn't found in {path.parent}")

    # cachemode=0 so no index files get written next to the user's masks, showprogress off so bestsource's
    # indexing chatter doesn't end up in the render log
    clip = core.bs.VideoSource(source=path, cachemode=0, showprogress=False)

    # take a single plane rather than converting - masks are greyscale, so this sidesteps needing a matrix for
    # rgb input, and handles gray, rgb and 16-bit pngs alike
    if clip.format.num_planes > 1:
        clip = core.std.ShufflePlanes(clip, 0, vs.GRAY)

    return clip[0]


# Automatic masks
#
# A HUD is the thing interpolation gets wrong, and it's also the one thing in the frame that doesn't move. So
# rather than asking for a png, `generate` samples frames from across the video and looks for the pixels that
# stayed put in nearly all of them, in two ways:
#
#  - held still (`_held_still`): the pixel is the same colour every time. this is what an opaque overlay looks
#    like - the money counter, weapon icons, panel backgrounds.
#  - stands out (`_local_contrast`): the pixel always sits on the same side of the pixels around it. an overlay
#    drawn with any transparency changes colour with whatever is behind it, so it never holds still, but a
#    white crosshair is still a light mark on its surroundings in every frame. scene content moves, so the
#    pixel it happens to sit on doesn't keep landing on the same side of its neighbours.
#
# A pixel counts as static if either test says so, which is what gets both the solid parts of a HUD and the
# translucent ones. Standing out is measured on colour as well as brightness - a green crosshair reads as
# brighter than a dark wall and darker than bright sand, so brightness alone never settles on an answer for it,
# while it stays the same green against both.
#
# Neither test is enough on its own, because a pixel with nothing drawn on it at all can pass them: an empty
# stretch of night sky is the same black in every frame, so it holds just as still as an opaque overlay. So
# there's a third thing every pixel has to satisfy, out of the same measurement standing out comes from:
#
#  - has detail (`_local_contrast`): the pixel differs from its surroundings at all, in either direction. an
#    overlay is something drawn onto the frame, so it does. flat sky doesn't. this one is asked of far fewer
#    of the samples than the other two - see MIN_DETAIL.
#
# That last one earns its keep well beyond tidying up the mask. A masked pixel shows the un-interpolated frame,
# so masked sky is sky that stops being interpolated - and the moment anything crosses it, a lantern drifting
# past or a tracer or a bird, that thing judders along its whole path while the rest of the frame stays smooth.
# The sky only ever read as static because nothing had happened there yet in the frames that got sampled.
#
# All three answer for the pixel itself and nothing further, which is what keeps the mask on the overlay rather
# than around it. That matters more than it sounds: a masked pixel shows the un-interpolated frame, so any part
# of the mask that lands on scene content freezes a halo of background that stutters while everything around it
# stays smooth. Overshooting is more visible than the artifact it covers up.

# frames pulled from across the clip to compare. each one is a seek and a decode, so this trades analysis time
# for confidence that what looks static really is
SAMPLE_COUNT = 24

# the analysis runs on the video shrunk to this height, if it's taller. more pixels doesn't buy better
# detection - shrinking averages compression noise away and turns a thin overlay stroke into something more
# solid, which reads more consistently - and every sampled frame is held in memory at once, so this is what
# stops that scaling with the source resolution
MAX_ANALYSIS_HEIGHT = 720

# skipped at each end, as a fraction of the clip. fades and title cards live there and have nothing in common
# with the rest of the video
SAMPLE_MARGIN = 0.02

# two samples this close to each other count as the same frame. everything here rests on the scene having
# moved between samples - an overlay only stands out because everything else changed - so frames from a
# stretch where nothing happened are worse than useless, they make the scene look as static as the overlay
SAMPLE_DIFFERENCE = 0.012

# when too many evenly spaced samples turn out to be near-duplicates, the search widens to a pool this many
# times larger and keeps the frames that do differ
SAMPLE_POOL_FACTOR = 3

# under this many usable samples there isn't enough to tell an overlay from a scene that happens to be sitting
# still, and no mask is made
MIN_SAMPLES = 8

# how much of the time a test has to hold for the pixel to count. below 1 so a single odd sample - a flashbang,
# a cut to black - doesn't disqualify an overlay that's there the rest of the time
MIN_CONSISTENCY = 0.9

# how far apart two samples of the same pixel can be (on a 0-1 scale) and still count as unchanged. compression
# noise moves even a perfectly static overlay around a little, so this can't be zero
SAME_PIXEL_THRESHOLD = 0.03

# how far a pixel has to sit from the average of the box around it to count as standing out from it, and how
# big that box is in pixels at 1080p, scaled from there. it wants to be a little wider than the strokes an
# overlay is drawn with, so a crosshair is measured against the scene rather than against itself, and no wider:
# a big box reaches over whatever is next to it, so a pixel of ordinary scene sat near a bright panel starts
# looking like it stands out too. the same distance applies to the colour planes, which span half the range
# brightness does, so it asks a bit more of them
LOCAL_CONTRAST = 0.01
LOCAL_RADIUS_PX_1080P = 3

# how much of the time a pixel has to differ from its surroundings to count as having something drawn on it.
# far below MIN_CONSISTENCY, because this is a different question: being still is what an overlay does the
# whole time, but being visible is only ever against the backgrounds it happens to sit on, and an opaque
# overlay disappears into any scene the same colour as itself. anything drawn clears this easily and an empty
# stretch of sky can't clear it at all, so there's a wide gap to sit in - the answer barely moves anywhere
# between a quarter and three quarters
MIN_DETAIL = 0.25

# how far the mask reaches into a flat region from the drawn detail around it, in pixels at 1080p. a flat patch
# enclosed by detail belongs to whatever is drawn around it - the middle of a solid panel, the gap between a
# counter's digits - so static pixels that failed the detail test are kept when they're this close to ones that
# passed. an expanse of sky is nowhere near anything drawn, so nothing reaches it; an overlay wider than twice
# this keeps an unmasked middle, which costs nothing, since a flat unchanging middle is the one thing
# interpolation can't get wrong
FILL_PX_1080P = 24

# a static pixel is thrown away unless this much of the box of radius CLUSTER_RADIUS around it is static too,
# which drops pixels that came back static entirely on their own - flat or dark scene that happened not to
# move. set so that a lone pixel goes and a pair stays, since the smallest real thing to find is a dot
# crosshair, and a 1px line fills a fifth of its box either way
CLUSTER_RADIUS = 2
CLUSTER_COVERAGE = 0.06

# how far the mask reaches past what was detected, and how far it ramps off after that, in analysis pixels.
# just enough to cover an overlay's own antialiased edge and to keep the boundary from being a hard seam -
# see the note above on why this doesn't want to be generous
GROW_PX = 1
FEATHER_PX = 1

# generated masks are kept here, under the settings folder alongside blur's other state, so a video is only
# ever analysed once however many previews and renders it goes through
CACHE_FOLDER = "auto-masks"

# how many to keep, oldest dropped first. a mask is mostly flat black and white so it compresses to a few
# kilobytes - the whole cache at this limit is a couple of megabytes, which is why the limit can be this loose
CACHE_LIMIT = 256

# a cached mask, and a cached "there was nothing to mask here". the mask is a png so that it's the same kind
# of file as a hand made one - copy it into the masks folder and it's a mask like any other
MASK_SUFFIX = ".png"
NOTHING_SUFFIX = ".nothing"

# under this much found, in pixels at 1080p, there's nothing worth protecting. an area rather than a share of
# the frame, because the smallest thing worth finding is a fixed size on screen - a dot crosshair - and not
# something that ought to shrink because the video is ultrawide. set below one of those: a dot crosshair is
# the smallest overlay there is and the one interpolation mangles worst, so it's what must not be missed
MIN_STATIC_PX_1080P = 16

# above this share of the frame, whatever was found is far bigger than a HUD - a locked-off shot, or a video
# that barely moves - and masking it would leave interpolation with nothing left to do, so the detection is
# treated as a miss
MAX_STATIC_FRACTION = 0.5


def _detached(clip: vs.VideoNode) -> vs.VideoNode:
    """Work out `clip`'s only frame now, and hand back a clip that does nothing but hold it.

    What this buys is that everything upstream can be freed the moment it returns. A mask still wired up to the
    analysis is a mask whose two dozen sampled frames, and every pass over them, sit in vapoursynth's cache for
    the length of the render - which is the point at which that memory is wanted for interpolation instead.
    """
    frame = clip.get_frame(0)

    # built from measurements rather than from `clip`, so the returned node has no path back to the analysis
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

    # a set because rounding can land two samples on the same frame in a very short clip
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

    # a handful of duplicates can't outvote MIN_CONSISTENCY between them, so they aren't worth another pass
    # over the video. past that the clip has real still stretches in it, and finding frames that moved means
    # casting a wider net
    if len(indices) >= count * MIN_CONSISTENCY:
        return indices

    pool = _differing(
        analysed, _sample_indices(analysed.num_frames, count * SAMPLE_POOL_FACTOR)
    )
    if len(pool) <= count:
        return pool

    # spread the survivors back out over the whole clip
    return [pool[round(i * (len(pool) - 1) / (count - 1))] for i in range(count)]


def _analysed(clip: vs.VideoNode) -> vs.VideoNode:
    """Get `clip` ready to measure: float YUV, full size colour planes, no taller than MAX_ANALYSIS_HEIGHT.

    Everything downstream is per-plane, and chroma is brought up to full size rather than the scores being
    scaled up afterwards, so that something as small as a crosshair dot is still a few pixels across in colour.
    """
    # full range both sides - nothing here gets displayed, and stretching limited range video into full would
    # just scale up the differences being measured
    convert = {"format": vs.YUV444PS, "range_s": "full"}

    if clip.format.color_family == vs.GRAY:
        convert |= {"format": vs.GRAYS, "range_in_s": "full"}
    elif clip.format.color_family == vs.RGB:
        # rgb has no brightness plane to take, so one has to be matrixed out of it. which matrix barely matters
        # - these values only ever get compared against each other
        convert |= {"matrix_s": "709"}
    else:
        convert |= {"range_in_s": "full"}

    if clip.height <= MAX_ANALYSIS_HEIGHT:
        return core.resize.Point(clip, **convert)

    # bilinear rather than point, so a thin overlay stroke comes through as a fainter stroke rather than being
    # sampled straight past
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


def _held_still(samples: list[vs.VideoNode]) -> vs.VideoNode:
    """How much of the time each pixel was the same colour as it was in the previous sample, 0-1.

    Consecutive pairs are compared rather than the spread over all the samples at once, so that one odd frame
    is survivable - it can only ever spoil the two pairs it belongs to.
    """
    counted = core.std.BlankClip(samples[0], color=0.0)
    for a, b in zip(samples, samples[1:]):
        counted = core.std.Expr(
            [counted, a, b], f"y z - abs {SAME_PIXEL_THRESHOLD} < x 1 + x ?"
        )

    return core.std.Expr(counted, f"x {len(samples) - 1} /")


def _local_contrast(
    samples: list[vs.VideoNode], radius: int
) -> tuple[vs.VideoNode, vs.VideoNode]:
    """Both measurements of how each pixel compares to its neighbours, as fractions of the samples, 0-1.

    Standing out is how much of the time the pixel sat on the *same* side of them. Above and below are counted
    separately and the better of the two is taken, so an overlay only has to be consistently one or the other.

    Having detail is how much of the time it sat on *either* side, which is to say how much of the time there
    was anything there to see at all. A pixel in a flat area sits on neither side and scores nothing in both.

    Each plane is measured on its own and the most convinced one wins the pixel, so an overlay is found by
    whichever of brightness or colour it actually differs from the scene in.
    """
    blank = core.std.BlankClip(samples[0], color=[0.0] * samples[0].format.num_planes)
    above, below = blank, blank

    for sample in samples:
        local = core.std.BoxBlur(sample, hradius=radius, vradius=radius)

        # the difference is worked out inside both counts rather than in a node of its own. every sample's
        # every intermediate is live at once while this runs, so a whole layer of them is worth not having
        above = core.std.Expr(
            [above, sample, local], f"y z - {LOCAL_CONTRAST} > x 1 + x ?"
        )
        below = core.std.Expr(
            [below, sample, local], f"y z - -{LOCAL_CONTRAST} < x 1 + x ?"
        )

    count = len(samples)

    # a pixel can't be on both sides in the same sample, so the sum is a count of samples like the max is
    stands_out = _best_plane(core.std.Expr([above, below], f"x y max {count} /"))
    has_detail = _best_plane(core.std.Expr([above, below], f"x y + {count} /"))

    return stands_out, has_detail


def generate(clip: vs.VideoNode) -> vs.VideoNode | None:
    """Work out a mask from `clip` by finding the parts of the frame that never change.

    Returns a one frame GRAY clip in the same convention as a mask png, or None when there was nothing usable
    to find - too short a clip, no static region, or so much of the frame static that it can't be an overlay.
    The caller should render without a mask in that case.
    """
    # this exact line, prefix included, is what blur watches for to show "analysing video" in place of
    # the render progress
    log.info("Generating mask")

    analysed = _analysed(clip)

    indices = _samples(analysed, SAMPLE_COUNT)
    if len(indices) < MIN_SAMPLES:
        log.info(
            f"Mask: only {len(indices)} frames of this video differ from each other, "
            "so there's nothing to tell an overlay apart from the scene. Rendering unmasked"
        )
        return None

    samples = [analysed[i] for i in indices]

    # the sizes below are all in analysis pixels. going through the analysis clip's own height rather than the
    # video's is what keeps them the same distance on screen whether or not the video got shrunk to fit
    scale = analysed.height / 1080

    def scaled(pixels_at_1080p: int) -> int:
        return max(1, round(pixels_at_1080p * scale))

    # only brightness is put through _held_still. colour is far flatter than brightness, so whole desaturated
    # stretches of a scene sit at the same chroma frame after frame and would come back as unchanged
    brightness = [_plane(sample, 0) for sample in samples]

    stands_out, has_detail = _local_contrast(samples, scaled(LOCAL_RADIUS_PX_1080P))

    # a pixel is static if either stillness test is convinced
    static = core.std.Expr(
        [_held_still(brightness), stands_out],
        f"x y max {MIN_CONSISTENCY} >= 1 0 ?",
    )

    # but it only counts where something was drawn to be still in the first place. an empty stretch of sky
    # holds perfectly still without being an overlay, and masking it is what stops anything that crosses it
    # later - a lantern, a tracer - from being interpolated at all
    drawn = core.std.Expr(has_detail, f"x {MIN_DETAIL} >= 1 0 ?")

    # a flat patch is kept anyway when there's detail within reach of it: the middle of a solid panel is as
    # flat as sky, but unlike sky it has the panel's own edges and text around it
    reach = scaled(FILL_PX_1080P)
    near_drawn = core.std.BoxBlur(
        core.std.Expr([static, drawn], "x y min"), hradius=reach, vradius=reach
    )
    static = core.std.Expr([static, near_drawn], "y 0 > x 0 ?")

    # throw away static pixels that are on their own, judged by how much of the box around each one is static
    density = core.std.BoxBlur(static, hradius=CLUSTER_RADIUS, vradius=CLUSTER_RADIUS)
    static = core.std.Expr([static, density], f"y {CLUSTER_COVERAGE} < 0 x ?")

    # the values are 0 or 1, so the average over the plane is the share of the frame that's static. this is the
    # point everything above actually gets decoded and run
    fraction = core.std.PlaneStats(static).get_frame(0).props["PlaneStatsAverage"]
    found = fraction * static.width * static.height

    # an area scales with the square of the linear scale the other sizes use
    if found < max(1, round(MIN_STATIC_PX_1080P * scale * scale)):
        log.info(
            f"Mask: nothing static found ({found:.0f} pixels). Rendering unmasked"
        )
        return None

    if fraction > MAX_STATIC_FRACTION:
        log.info(
            f"Mask: {fraction:.1%} of the frame is static, far too much of it to be an overlay. "
            "Rendering unmasked"
        )
        return None

    log.info(f"Mask: protecting the {fraction:.2%} of the frame that never moves")

    # the blur's ramp is centred on the edge it's given, so growing by GROW_PX + FEATHER_PX first leaves
    # everything within GROW_PX of the detection fully protected once the ramp has eaten back into it
    for _ in range(GROW_PX + FEATHER_PX):
        static = core.std.Maximum(static)

    static = core.std.BoxBlur(static, hradius=FEATHER_PX, vradius=FEATHER_PX)

    # masks read the other way round: white means "interpolate this as normal"
    return _detached(core.std.Expr(static, "1 x -"))


# Caching
#
# A config preview re-runs this whole script for every setting the user nudges and every seek, so without
# somewhere to put the answer the video would be analysed again each time. Masks are static, so the answer
# only depends on the video and on how the analysis works, and both of those go into the key.


def _cache_key(video_path: Path, analysed: tuple[int, int]) -> str:
    """Identify a video, the part of it being analysed, and the analysis that would be run on it.

    This module's own source is part of the key, so any change to how a mask is worked out - a retuned
    constant included - leaves old cached masks behind rather than quietly reusing them.
    """
    stat = video_path.stat()
    subject = f"{video_path.resolve()}\n{stat.st_size}\n{stat.st_mtime_ns}\n{analysed}".encode()

    return hashlib.sha1(subject + Path(__file__).read_bytes()).hexdigest()[:16]


def _cache_name(video_path: Path, key: str) -> str:
    """`key` is what identifies the file; the video's name is in there to make the folder readable."""
    readable = "".join(c if c.isalnum() or c in "-_" else "_" for c in video_path.stem)[
        :48
    ]

    return f"{readable}-{key}"


def _greyscale_png(plane, width: int, height: int) -> bytes:
    """Encode a single 8 bit plane as a png.

    Hand rolled because none of the plugins blur ships can write an image, and a png is worth the twenty lines
    - it's what a mask is normally, so a generated one can be opened, touched up and kept like any other.

    Scanlines go in unfiltered. Filtering exists to help the compressor find patterns, and a mask is mostly
    flat runs of black and white, which zlib already handles about as well as it's going to.
    """
    packed = (
        plane.tobytes()
    )  # tobytes drops the stride padding, leaving exactly width bytes a row
    scanlines = b"".join(
        b"\x00" + packed[y * width : (y + 1) * width] for y in range(height)
    )

    def chunk(kind: bytes, body: bytes) -> bytes:
        return (
            struct.pack(">I", len(body))
            + kind
            + body
            + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
        )

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
    """Write a generated mask out, or note that there wasn't one worth having."""
    if gray is None:
        path.with_suffix(NOTHING_SUFFIX).write_bytes(b"")
        return

    # 8 bit is what a mask png is, and all a coverage value needs
    eight_bit = core.resize.Point(
        gray, format=vs.GRAY8, range_in_s="full", range_s="full", dither_type="none"
    )
    png = _greyscale_png(eight_bit.get_frame(0)[0], eight_bit.width, eight_bit.height)

    # written alongside and moved into place, so a half written file is never picked up as a cached mask
    partial = path.with_suffix(".partial")
    partial.write_bytes(png)
    partial.replace(path.with_suffix(MASK_SUFFIX))


def _prune(folder: Path):
    """Drop the oldest cached masks once there are more than CACHE_LIMIT of them."""
    kept = sorted(
        (p for p in folder.iterdir() if p.suffix in (MASK_SUFFIX, NOTHING_SUFFIX)),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )

    for stale in kept[CACHE_LIMIT:]:
        stale.unlink(missing_ok=True)


def cached(
    clip: vs.VideoNode, video_path: Path, folder: Path, analysed: tuple[int, int]
) -> vs.VideoNode | None:
    """`generate`, but remembering the answer on disk between runs.

    `analysed` is which frames of `video_path` the clip covers. It's only used to tell cached masks apart:
    trimming to a different part of a video can turn up a different overlay, so it can't share one.

    Comes back 8 bit rather than float when it's read from the cache, which `match` handles either way.

    Falls back to generating in place if the cache can't be read or written - a cache that isn't working is
    not a reason to fail a render.
    """
    try:
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / _cache_name(video_path, _cache_key(video_path, analysed))
    except OSError as e:
        log.info(f"Mask: can't use the cache ({e}), analysing this video every time")
        return generate(clip)

    if path.with_suffix(NOTHING_SUFFIX).exists():
        log.info("Mask: cached as nothing worth masking")
        return None

    mask_file = path.with_suffix(MASK_SUFFIX)
    if mask_file.exists():
        try:
            log.info(f"Mask: reusing {mask_file.name}")
            return load(mask_file)
        except Exception as e:
            # unreadable, so it's no better than not having it
            log.info(f"Mask: couldn't read {mask_file.name} ({e}), analysing again")

    gray = generate(clip)

    try:
        _store(gray, path)
        _prune(folder)
    except OSError as e:
        log.info(f"Mask: couldn't save to the cache ({e})")

    return gray


def match(gray: vs.VideoNode, clip: vs.VideoNode) -> vs.VideoNode:
    """Scale a GRAY mask to `clip`'s dimensions and build it into `clip`'s exact format.

    MaskedMerge's handling of a grayscale mask against a subsampled clip is fiddly, so instead of relying on it
    the mask is turned into a full clip of the same format - one plane per plane, each at the right size.
    """
    fmt = clip.format

    luma_format = core.query_video_format(
        vs.GRAY, fmt.sample_type, fmt.bits_per_sample, 0, 0
    )

    def resized(width: int, height: int) -> vs.VideoNode:
        # full range both sides - these are coverage values, not video levels, and must not get scaled
        # into the limited range on the way to a higher bit depth
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

    return core.std.ShufflePlanes(
        [luma, chroma, chroma], planes=[0, 0, 0], colorfamily=fmt.color_family
    )


def match_length(src: vs.VideoNode, target: vs.VideoNode) -> vs.VideoNode:
    """Stretch `src` over `target`'s frame count by repeating frames.

    Frame indices are mapped directly rather than going through change_fps, which takes an integer fps -
    interpolated framerates can be fractional when they come from a multiplier like '5x'.
    """
    if (
        src.format.id != target.format.id
        or src.width != target.width
        or src.height != target.height
    ):
        src = core.resize.Bicubic(
            src, width=target.width, height=target.height, format=target.format.id
        )

    if src.num_frames == target.num_frames:
        return src

    if src.num_frames == 0:
        raise u.BlurException("Can't apply a mask to an empty clip")

    factor = target.num_frames / src.num_frames
    last = src.num_frames - 1

    return core.std.FrameEval(
        core.std.BlankClip(target), lambda n: src[min(round(n / factor), last)]
    )


def combine(grays: list[vs.VideoNode]) -> vs.VideoNode:
    """Stack GRAY masks: a pixel is protected if any of them protects it.

    They won't be the same size - a base mask is whatever resolution it was drawn at, and a generated one comes
    back at the analysis height - so everything is brought up to the largest of them first. Up rather than down
    because a mask is applied at the video's own resolution anyway, and shrinking one here would throw away
    detail that `match` is about to ask for again.
    """
    if len(grays) == 1:
        return grays[0]

    width = max(gray.width for gray in grays)
    height = max(gray.height for gray in grays)

    def sized(gray: vs.VideoNode) -> vs.VideoNode:
        # full range both sides - these are coverage values, not video levels
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
        # black is what protects, so the darker of the two wins the pixel
        combined = core.std.Expr([combined, sized(gray)], "x y min")

    return combined


def protect(
    interpolated: vs.VideoNode, original: vs.VideoNode, gray: vs.VideoNode
) -> vs.VideoNode:
    """Put `original`'s pixels back wherever the mask is black."""
    base = match_length(original, interpolated)

    # white takes clipb (interpolated), black takes clipa (original)
    return core.std.MaskedMerge(base, interpolated, match(gray, interpolated))
