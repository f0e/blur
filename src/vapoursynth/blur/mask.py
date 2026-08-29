"""Masks protect regions of the frame from interpolation.

The interpolator has no idea a HUD isn't part of the scene, so it warps it along with the world behind it.
A mask marks those regions, and the pre-interpolation frames get put back over them afterwards - so they never
get warped, while still going through frame blending like everything else.

A mask is a png in <settings path>/masks: white where the frame should be interpolated as normal, black where
it should be left alone. The "auto" setting skips the png and works one out from the video itself - see
`generate`.
"""

from vapoursynth import core
import vapoursynth as vs

from pathlib import Path

import blur.utils as u


def load(path: Path) -> vs.VideoNode:
    """Read a mask png into a one frame GRAY clip."""
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
#  - `_held_still`: the pixel is the same colour every time. this is what an opaque overlay looks like - the
#    money counter, weapon icons, panel backgrounds.
#  - `_stands_out`: the pixel is always brighter (or always darker) than the pixels around it. an overlay drawn
#    with any transparency changes colour with whatever is behind it, so it never holds still, but a white
#    crosshair is still a light mark on its surroundings in every frame. scene content moves, so the pixel it
#    happens to sit on doesn't keep landing on the same side of its neighbours.
#
# A pixel counts as static if either test says so, which is what gets both the solid parts of a HUD and the
# translucent ones. `_stands_out` runs twice, against a tight neighbourhood and a wide one - see
# TIGHT_RADIUS_PX_1080P.

# what the `mask` setting is set to to ask for this instead of a file
AUTO = "auto"

# frames pulled from across the clip to compare. each one is a seek and a decode, so this trades analysis time
# for confidence that what looks static really is
SAMPLE_COUNT = 24

# skipped at each end, as a fraction of the clip. fades and title cards live there and have nothing in common
# with the rest of the video
SAMPLE_MARGIN = 0.02

# how much of the time a test has to hold for the pixel to count. below 1 so a single odd sample - a flashbang,
# a cut to black - doesn't disqualify an overlay that's there the rest of the time
MIN_CONSISTENCY = 0.9

# how far apart two samples of the same pixel can be (on a 0-1 scale) and still count as unchanged. compression
# noise moves even a perfectly static overlay around a little, so this can't be zero
SAME_PIXEL_THRESHOLD = 0.03

# how far a pixel has to sit from the average of the box around it to count as standing out from it, and how
# big those boxes are in pixels at 1080p, scaled from there. two sizes, because they pick up different things:
# the tight box is what registers a crosshair or thin text, which a wide box averages away, and the wide box is
# what fills in the middle of a solid panel, where a tight box sees nothing but more panel
LOCAL_CONTRAST = 0.01
TIGHT_RADIUS_PX_1080P = 3
WIDE_RADIUS_PX_1080P = 9

# a static pixel is thrown away unless this much of the box of radius CLUSTER_RADIUS around it is static too.
# lone pixels are noise - flat or dark areas that happened not to move - and growing them would turn each one
# into a blob. a 1px crosshair line still fills a fifth of its box, so thin overlays survive this
CLUSTER_RADIUS = 2
CLUSTER_COVERAGE = 0.12

# how far the mask reaches past what was detected, in pixels at 1080p and scaled from there. interpolation
# drags pixels in from the surrounding area, so the border around an overlay needs protecting too. GROW is
# fully protected and FEATHER ramps off after it, so the edge of the mask isn't a hard seam
GROW_PX_1080P = 4
FEATHER_PX_1080P = 4

# below this fraction of the frame there's nothing worth protecting. above it, whatever was found is far bigger
# than a HUD - a locked-off shot, or a video that barely moves - and masking it would leave interpolation with
# nothing left to do, so the detection is treated as a miss either way
MIN_STATIC_FRACTION = 0.0001
MAX_STATIC_FRACTION = 0.5


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


def _luma(clip: vs.VideoNode) -> vs.VideoNode:
    """Get `clip`'s brightness as a float GRAY clip, so samples can be compared on a 0-1 scale."""
    if clip.format.color_family == vs.RGB:
        # rgb has no luma plane to take, so one has to be matrixed out of it. which matrix barely matters -
        # these values only ever get compared against each other
        return core.resize.Point(clip, format=vs.GRAYS, matrix_s="709", range_s="full")

    gray = core.std.ShufflePlanes(clip, 0, vs.GRAY)

    # full range both sides - nothing here gets displayed, and stretching limited range video into full would
    # just scale up the differences being measured
    return core.resize.Point(gray, format=vs.GRAYS, range_in_s="full", range_s="full")


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


def _stands_out(samples: list[vs.VideoNode], radius: int) -> vs.VideoNode:
    """How much of the time each pixel sat on the same side of its neighbours, 0-1.

    Brighter and darker are counted separately and the better of the two is taken, so an overlay only has to be
    consistently one or the other. A pixel in a flat area sits on neither side and scores nothing.
    """
    brighter = core.std.BlankClip(samples[0], color=0.0)
    darker = core.std.BlankClip(samples[0], color=0.0)

    for sample in samples:
        local = core.std.BoxBlur(sample, hradius=radius, vradius=radius)
        offset = core.std.Expr([sample, local], "x y -")

        brighter = core.std.Expr([brighter, offset], f"y {LOCAL_CONTRAST} > x 1 + x ?")
        darker = core.std.Expr([darker, offset], f"y -{LOCAL_CONTRAST} < x 1 + x ?")

    return core.std.Expr([brighter, darker], f"x y max {len(samples)} /")


def generate(clip: vs.VideoNode) -> vs.VideoNode | None:
    """Work out a mask from `clip` by finding the parts of the frame that never change.

    Returns a one frame GRAY clip in the same convention as a mask png, or None when there was nothing usable
    to find - too short a clip, no static region, or so much of the frame static that it can't be an overlay.
    The caller should render without a mask in that case.
    """
    indices = _sample_indices(clip.num_frames, SAMPLE_COUNT)
    if len(indices) < 2:
        u.log("auto mask: clip is too short to analyse")
        return None

    scale = clip.height / 1080

    def scaled(pixels_at_1080p: int) -> int:
        return max(1, round(pixels_at_1080p * scale))

    luma = _luma(clip)
    samples = [luma[i] for i in indices]

    # a pixel is static if any of the tests is convinced
    static = core.std.Expr(
        [
            _held_still(samples),
            _stands_out(samples, scaled(TIGHT_RADIUS_PX_1080P)),
            _stands_out(samples, scaled(WIDE_RADIUS_PX_1080P)),
        ],
        f"x y max z max {MIN_CONSISTENCY} >= 1 0 ?",
    )

    # throw away static pixels that are on their own, judged by how much of the box around each one is static
    density = core.std.BoxBlur(static, hradius=CLUSTER_RADIUS, vradius=CLUSTER_RADIUS)
    static = core.std.Expr([static, density], f"y {CLUSTER_COVERAGE} < 0 x ?")

    # the values are 0 or 1, so the average over the plane is the share of the frame that's static. this is the
    # point everything above actually gets decoded and run
    fraction = core.std.PlaneStats(static).get_frame(0).props["PlaneStatsAverage"]

    if fraction < MIN_STATIC_FRACTION:
        u.log(
            f"auto mask: found nothing static ({fraction:.4%} of the frame), not masking"
        )
        return None

    if fraction > MAX_STATIC_FRACTION:
        u.log(
            f"auto mask: {fraction:.1%} of the frame is static, too much of it to be an overlay, not masking"
        )
        return None

    u.log(f"auto mask: {fraction:.3%} of the frame is static")

    grow = scaled(GROW_PX_1080P)
    feather = scaled(FEATHER_PX_1080P)

    # the blur's ramp is centred on the edge it's given, so growing by grow + feather first leaves everything
    # within grow of the detection fully protected once feather has eaten back into it
    for _ in range(grow + feather):
        static = core.std.Maximum(static)

    static = core.std.BoxBlur(static, hradius=feather, vradius=feather)

    # masks read the other way round: white means "interpolate this as normal"
    return core.std.Expr(static, "1 x -")


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


def protect(
    interpolated: vs.VideoNode, original: vs.VideoNode, gray: vs.VideoNode
) -> vs.VideoNode:
    """Put `original`'s pixels back wherever the mask is black."""
    base = match_length(original, interpolated)

    # white takes clipb (interpolated), black takes clipa (original)
    return core.std.MaskedMerge(base, interpolated, match(gray, interpolated))
