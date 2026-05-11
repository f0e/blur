from vapoursynth import core
import vapoursynth as vs

import blur.utils as u


# https://github.com/AkarinVS/vapoursynth-plugin/issues/17#issuecomment-1312639376
# can't use Expr2 which supports src0,1,2 etc. when using asmjit so youre limited to 26 clips
# this is a workaround
def expr1_arbitrary_weights_blend(clips, weights):
    names = "".join(map(lambda x: chr(x + ord("a")), range(26)))
    names = names[-3:] + names[:-3]  # 'xyzabc...w'
    limit = 26
    if len(clips) <= limit:
        expr = " ".join(
            map(lambda cw: f"{cw[0]} {cw[1]} *", zip(names[: len(clips)], weights))
        ) + " +" * (len(clips) - 1)

        return core.akarin.Expr(
            clips,
            expr,
        )
    else:
        return expr1_arbitrary_weights_blend(
            [expr1_arbitrary_weights_blend(clips[:limit], weights[:limit])]
            + clips[limit:],
            [1.0] + weights[limit:],
        )


def average_expr1(
    clip: vs.VideoNode, weights: list[float], divisor: float | None = None
):
    def get_offset_clip(offset: int) -> vs.VideoNode:
        if offset > 0:
            return clip[offset:] + clip[-1] * offset
        elif offset < 0:
            return clip[0] * -offset + clip[:offset]
        else:
            return clip

    diameter = len(weights)
    radius = diameter // 2

    if divisor is None:
        divisor = sum(weights)

    assert diameter % 2 == 1, "An odd number of weights is required."

    clips = [get_offset_clip(offset) for offset in range(-radius, radius + 1)]

    # todo: divisor? do u need it?
    return expr1_arbitrary_weights_blend(clips, weights)


# https://github.com/couleur-tweak-tips/smoothie-rs/blob/main/target/scripts/blending.py
def average(clip: vs.VideoNode, weights: list[float], divisor: float | None = None):
    def get_offset_clip(offset: int) -> vs.VideoNode:
        if offset > 0:
            return clip[offset:] + clip[-1] * offset
        elif offset < 0:
            return clip[0] * -offset + clip[:offset]
        else:
            return clip

    diameter = len(weights)
    radius = diameter // 2

    if divisor is None:
        divisor = sum(weights)

    assert diameter % 2 == 1, "An odd number of weights is required."

    clips = [get_offset_clip(offset) for offset in range(-radius, radius + 1)]

    expr = ""
    for i in range(0, diameter):
        expr += f"src{i} {weights[i]} * "

    expr += "+ " * (diameter - 1)
    expr += f"{divisor} /" if divisor != 1 else ""

    return core.akarin.Expr(clips, expr)


def average_bright(
    _video: vs.VideoNode,
    video_info: u.VideoInfo,
    gamma: float,
    weights: list[float],
    divisor: float | None = None,
):
    def process(video):
        def gamma_correct(video, gamma):
            expr = f"x {gamma} pow"
            return core.std.Expr(video, expr=expr)
            # return core.std.Levels(
            #     video, gamma=gamma, min_in=0.0, max_in=1.0, min_out=0.0, max_out=1.0
            # )

        video = gamma_correct(video, gamma)
        video = average(video, weights, divisor)
        video = gamma_correct(video, 1.0 / gamma)

        return video

    return u.with_format(
        _video,
        video_info,
        vs.RGBS,
        process,
    )
