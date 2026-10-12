import blur.utils as u
import vapoursynth as vs
from blur import scenes
from vapoursynth import core

# frameblender takes yuv in for the squared blend from this version, with these matrices
YUV_SQUARED_VERSION = (3, 1)
BLENDER_MATRICES = {vs.MATRIX_BT709, vs.MATRIX_BT470_BG, vs.MATRIX_ST170_M, vs.MATRIX_BT2020_NCL}

# and blends hdr under its own curve from this one
HDR_VERSION = (3, 2)

# the square is within ~7 of 255 levels of srgb, so it's used for all sdr footage (an exact srgb curve made a whole
# 1080p render 15-20% slower). on pq it's off by up to 224 of 1023 levels, so hdr gets its real curve
HDR_TRANSFERS = {vs.TRANSFER_ST2084: "pq", vs.TRANSFER_ARIB_B67: "hlg"}


def curve_args(video_info: u.VideoInfo) -> dict:
    """FrameBlend's arguments for blending the light the source's values encode."""
    transfer = HDR_TRANSFERS.get(video_info.transfer)
    if transfer and tuple(core.frameblender.version) >= HDR_VERSION:
        return {"transfer": transfer}

    return {"squared": True}


def average(
    clip: vs.VideoNode,
    weights: list[float],
    divisor: float | None = None,
    curve: dict | None = None,
    cuts: vs.VideoNode | None = None,
):
    assert len(weights) % 2 == 1, "An odd number of weights is required."

    kwargs = dict(curve or {})
    if divisor is not None:
        kwargs["scale"] = divisor

    def frame_blend(clip: vs.VideoNode) -> vs.VideoNode:
        return core.frameblender.FrameBlend(clip, weights, **kwargs)

    if cuts is None:
        return frame_blend(clip)

    return scenes.within(clip, len(weights) // 2, cuts, frame_blend)


def bloom(clip: vs.VideoNode, threshold: float, strength: float):
    BLOOM_SCALES = (8, 32)  # small central bloom and larger outer bloom

    excess = core.std.Expr(clip, f"x {threshold} - 0 max")

    spread = None
    for scale in BLOOM_SCALES:
        w = max(2, (clip.width // scale) & ~1)
        h = max(2, (clip.height // scale) & ~1)

        layer = core.resize.Bilinear(excess, width=w, height=h)
        layer = core.resize.Bilinear(layer, width=clip.width, height=clip.height)

        spread = layer if spread is None else core.std.Expr([spread, layer], "x y +")

    return core.std.Expr([clip, spread], f"x y {strength / len(BLOOM_SCALES)} * +")


def blend(
    _video: vs.VideoNode,
    video_info: u.VideoInfo,
    weights: list[float],
    preserve_brightness: bool,
    bloom_threshold: float,
    bloom_strength: float | None,
    divisor: float | None = None,
    cuts: vs.VideoNode | None = None,
):
    if not preserve_brightness and bloom_strength is None:
        return average(_video, weights, divisor, cuts=cuts)

    def process(video):
        if bloom_strength is not None:
            video = bloom(video, bloom_threshold, bloom_strength)

        return average(video, weights, divisor, curve_args(video_info) if preserve_brightness else None, cuts)

    if bloom_strength is not None:
        return u.with_format(
            _video,
            video_info,
            vs.RGBS,  # bloom adds light, so highlights need room above white
            process,
        )

    matrix = vs.MATRIX_UNSPECIFIED
    if _video.format.color_family == vs.YUV and tuple(core.frameblender.version) >= YUV_SQUARED_VERSION:
        matrix = _video.get_frame(0).props.get("_Matrix", vs.MATRIX_UNSPECIFIED)
        if matrix == vs.MATRIX_UNSPECIFIED:
            matrix = u.guess_matrix(_video.width, _video.height)

    if matrix in BLENDER_MATRICES and _video.format.subsampling_w <= 1 and _video.format.subsampling_h <= 1:
        # the blender converts to rgb for the curve itself, which saves converting every interpolated frame there
        # and back. it reads black, white and the matrix from the props
        _video = core.std.SetFrameProps(
            _video,
            _Matrix=matrix,
            _ColorRange=vs.RANGE_FULL if video_info.is_full_color_range else vs.RANGE_LIMITED,
        )
        return average(_video, weights, divisor, curve_args(video_info), cuts)

    return u.with_format(
        _video,
        video_info,
        # the curve runs per channel, so rgb. integer is enough - the blend accumulates in float anyway
        vs.RGB24 if _video.format.bits_per_sample <= 8 else vs.RGB48,
        process,
        expand_range=False,  # keeps superwhite, which the curve exists to protect
    )
