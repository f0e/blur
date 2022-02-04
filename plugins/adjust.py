import vapoursynth as vs
import math


# XXX bright should be a percentage or something


def Tweak(clip, hue=None, sat=None, bright=None, cont=None, coring=True):
    if clip.format is None:
        raise vs.Error("Tweak: only clips with constant format are accepted.")

    if clip.format.color_family == vs.RGB:
        raise vs.Error("Tweak: RGB clips are not accepted.")

    c = vs.get_core()
        
    if (hue is not None or sat is not None) and clip.format.color_family != vs.GRAY:
        hue = 0.0 if hue is None else hue
        sat = 1.0 if sat is None else sat

        hue = hue * math.pi / 180.0
        hue_sin = math.sin(hue)
        hue_cos = math.cos(hue)

        gray = 128 << (clip.format.bits_per_sample - 8)

        chroma_min = 0
        chroma_max = (2 ** clip.format.bits_per_sample) - 1
        if coring:
            chroma_min = 16 << (clip.format.bits_per_sample - 8)
            chroma_max = 240 << (clip.format.bits_per_sample - 8)

        expr_u = "x {} - {} * y {} - {} * + {} + {} max {} min".format(gray, hue_cos * sat, gray, hue_sin * sat, gray, chroma_min, chroma_max)
        expr_v = "y {} - {} * x {} - {} * - {} + {} max {} min".format(gray, hue_cos * sat, gray, hue_sin * sat, gray, chroma_min, chroma_max)

        if clip.format.sample_type == vs.FLOAT:
            expr_u = "x {} * y {} * + -0.5 max 0.5 min".format(hue_cos * sat, hue_sin * sat)
            expr_v = "y {} * x {} * - -0.5 max 0.5 min".format(hue_cos * sat, hue_sin * sat)

        src_u = clip.std.ShufflePlanes(planes=1, colorfamily=vs.GRAY)
        src_v = clip.std.ShufflePlanes(planes=2, colorfamily=vs.GRAY)

        dst_u = c.std.Expr(clips=[src_u, src_v], expr=expr_u)
        dst_v = c.std.Expr(clips=[src_u, src_v], expr=expr_v)

        clip = c.std.ShufflePlanes(clips=[clip, dst_u, dst_v], planes=[0, 0, 0], colorfamily=clip.format.color_family)

    if bright is not None or cont is not None:
        bright = 0.0 if bright is None else bright
        cont = 1.0 if cont is None else cont

        if clip.format.sample_type == vs.INTEGER:
            luma_lut = []

            luma_min = 0
            luma_max = (2 ** clip.format.bits_per_sample) - 1
            if coring:
                luma_min = 16 << (clip.format.bits_per_sample - 8)
                luma_max = 235 << (clip.format.bits_per_sample - 8)

            for i in range(2 ** clip.format.bits_per_sample):
                val = int((i - luma_min) * cont + bright + luma_min + 0.5)
                luma_lut.append(min(max(val, luma_min), luma_max))

            clip = clip.std.Lut(planes=0, lut=luma_lut)
        else:
            expression = "x {} * {} + 0.0 max 1.0 min".format(cont, bright)

            clip = clip.std.Expr(expr=[expression, "", ""])

    return clip
