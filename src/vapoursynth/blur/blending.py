from vapoursynth import core
import vapoursynth as vs

import blur.utils as u


def average(
    clip: vs.VideoNode,
    weights: list[float],
    divisor: float | None = None,
    squared: bool = False,
):
    assert len(weights) % 2 == 1, "An odd number of weights is required."

    kwargs = {}
    if divisor is not None:
        kwargs["scale"] = divisor
    if squared:
        kwargs["squared"] = True

    return core.frameblender.FrameBlend(clip, weights, **kwargs)


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
):
    if not preserve_brightness and bloom_strength is None:
        return average(_video, weights, divisor)

    def process(video):
        if bloom_strength is not None:
            video = bloom(video, bloom_threshold, bloom_strength)

        return average(video, weights, divisor, preserve_brightness)

    if bloom_strength is not None:
        return u.with_format(
            _video,
            video_info,
            vs.RGBS,  # bloom adds light, so highlights need room above white
            process,
        )

    return u.with_format(
        _video,
        video_info,
        # the curve runs per channel, so rgb. integer is enough - the blend accumulates in float anyway
        vs.RGB24 if _video.format.bits_per_sample <= 8 else vs.RGB48,
        process,
        expand_range=False,  # keeps superwhite, which the curve exists to protect
    )
