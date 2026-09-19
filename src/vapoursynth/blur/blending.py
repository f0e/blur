from vapoursynth import core
import vapoursynth as vs

import blur.utils as u


def average(
    clip: vs.VideoNode,
    weights: list[float],
    divisor: float | None = None,
    gamma: float = 1.0,
):
    assert len(weights) % 2 == 1, "An odd number of weights is required."

    kwargs = {}
    if divisor is not None:
        kwargs["scale"] = divisor
    if gamma != 1.0:
        kwargs["gamma"] = gamma

    return core.frameblender.FrameBlend(clip, weights, **kwargs)


def average_bright(
    _video: vs.VideoNode,
    video_info: u.VideoInfo,
    gamma: float,
    weights: list[float],
    divisor: float | None = None,
):
    # the blend applies the curve itself, so all this has to do is get the video into rgb. it has to
    # be rgb and not luma: the curve runs per channel, or bright saturated things come out bright and
    # washed out.
    #
    # the video keeps its own range on the way in, and the blend anchors the curve to whatever
    # _ColorRange says. expanding limited range to fill the container instead would clip every
    # sample above white, which is exactly what this is supposed to be protecting
    return u.with_format(
        _video,
        video_info,
        vs.RGB24 if _video.format.bits_per_sample <= 8 else vs.RGB48,
        lambda video: average(video, weights, divisor, gamma),
        expand_range=False,
    )
