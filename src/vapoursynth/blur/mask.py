"""Masks protect regions of the frame from interpolation.

The interpolator has no idea a HUD isn't part of the scene, so it warps it along with the world behind it.
A mask marks those regions, and the pre-interpolation frames get put back over them afterwards - so they never
get warped, while still going through frame blending like everything else.

A mask is a png in <settings path>/masks: white where the frame should be interpolated as normal, black where
it should be left alone.
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
