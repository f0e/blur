from functools import partial
import math

from vapoursynth import core
import vapoursynth as vs
import mvsfunc as mvf
import adjust

"""
Holy's ported AviSynth functions for VapourSynth.

Main functions:
    daa
    daa3mod
    mcdaa3
    santiag
    FixChromaBleedingMod
    Deblock_QED
    DeHalo_alpha
    EdgeCleaner
    FineDehalo, FineDehalo2
    YAHR
    HQDeringmod
    QTGMC
    smartfademod
    srestore
    dec_txt60mc
    ivtc_txt30mc
    ivtc_txt60mc
    logoNR
    Vinverse
    Vinverse2
    LUTDeCrawl
    LUTDeRainbow
    Stab
    GrainStabilizeMC
    MCTemporalDenoise
    SMDegrain
    STPresso
    bbmod
    SigmoidInverse, SigmoidDirect
    GrainFactory3
    InterFrame
    FixColumnBrightness, FixRowBrightness
    FixColumnBrightnessProtect, FixRowBrightnessProtect
    FixColumnBrightnessProtect2, FixRowBrightnessProtect2
    SmoothLevels
    FastLineDarkenMOD
    Toon
    LSFmod
    TemporalDegrain
    aaf

Utility functions:
    AverageFrames
    AvsPrewitt
    Bob
    ChangeFPS
    Clamp
    KNLMeansCL
    Overlay
    Padding
    Resize
    SCDetect
    Weave
    ContraSharpening
    MinBlur
    sbr, sbrV
    DitherLumaRebuild
    mt_expand_multi, mt_inpand_multi
    mt_inflate_multi, mt_deflate_multi
"""




##################
#                #
# Main functions #
#                #
##################


# Anti-aliasing with contra-sharpening by Didée
def daa(c, nsize=None, nns=None, qual=None, pscrn=None, int16_prescreener=None, int16_predictor=None, exp=None, opencl=False, device=None):
    if not isinstance(c, vs.VideoNode):
        raise vs.Error('daa: This is not a clip')

    if opencl:
        nnedi3 = partial(core.nnedi3cl.NNEDI3CL, nsize=nsize, nns=nns, qual=qual, pscrn=pscrn, device=device)
    else:
        nnedi3 = partial(core.znedi3.nnedi3 if hasattr(core, 'znedi3') else core.nnedi3.nnedi3,
                         nsize=nsize, nns=nns, qual=qual, pscrn=pscrn, int16_prescreener=int16_prescreener, int16_predictor=int16_predictor, exp=exp)

    nn = nnedi3(c, field=3)
    dbl = core.std.Merge(nn[::2], nn[1::2])
    dblD = core.std.MakeDiff(c, dbl)
    shrpD = core.std.MakeDiff(dbl, dbl.std.Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1] if c.width > 1100 else [1, 2, 1, 2, 4, 2, 1, 2, 1]))
    DD = core.rgvs.Repair(shrpD, dblD, mode=[13])
    return core.std.MergeDiff(dbl, DD)


def daa3mod(c1, nsize=None, nns=None, qual=None, pscrn=None, int16_prescreener=None, int16_predictor=None, exp=None, opencl=False, device=None):
    if not isinstance(c1, vs.VideoNode):
        raise vs.Error('daa3mod: This is not a clip')

    c = c1.resize.Spline36(c1.width, c1.height * 3 // 2)
    return daa(c, nsize, nns, qual, pscrn, int16_prescreener, int16_predictor, exp, opencl, device).resize.Spline36(c1.width, c1.height)


def mcdaa3(input, nsize=None, nns=None, qual=None, pscrn=None, int16_prescreener=None, int16_predictor=None, exp=None, opencl=False, device=None):
    if not isinstance(input, vs.VideoNode):
        raise vs.Error('mcdaa3: This is not a clip')

    sup = input.hqdn3d.Hqdn3d().fft3dfilter.FFT3DFilter().mv.Super(sharp=1)
    fv1 = sup.mv.Analyse(isb=False, delta=1, truemotion=False, dct=2)
    fv2 = sup.mv.Analyse(isb=True, delta=1, truemotion=True, dct=2)
    csaa = daa3mod(input, nsize, nns, qual, pscrn, int16_prescreener, int16_predictor, exp, opencl, device)
    momask1 = input.mv.Mask(fv1, ml=2, kind=1)
    momask2 = input.mv.Mask(fv2, ml=3, kind=1)
    momask = core.std.Merge(momask1, momask2)
    return core.std.MaskedMerge(input, csaa, momask)


# santiag v1.6
# Simple antialiasing
#
# This program is free software. It comes without any warranty, to
# the extent permitted by applicable law. You can redistribute it
# and/or modify it under the terms of the Do What The Fuck You Want
# To Public License, Version 2, as published by Sam Hocevar. See
# http://sam.zoy.org/wtfpl/COPYING for more details.
#
# type = "nnedi3", "eedi2", "eedi3" or "sangnom"
def santiag(c, strh=1, strv=1, type='nnedi3', nsize=None, nns=None, qual=None, pscrn=None, int16_prescreener=None, int16_predictor=None, exp=None, aa=None,
            alpha=None, beta=None, gamma=None, nrad=None, mdis=None, vcheck=None, fw=None, fh=None, halfres=False, typeh=None, typev=None, opencl=False, device=None):
    def santiag_dir(c, strength, type, fw=None, fh=None):
        if fw is None:
            fw = c.width
        if fh is None:
            fh = c.height

        c = santiag_stronger(c, strength, type)

        cshift = 0.0 if halfres else 0.5
        if c.format.color_family != vs.GRAY:
            cshift = [cshift, cshift * (1 << c.format.subsampling_h)]
        return Resize(c, fw, fh, sy=cshift, dmode=1)

    def santiag_stronger(c, strength, type):
        if opencl:
            nnedi3 = partial(core.nnedi3cl.NNEDI3CL, nsize=nsize, nns=nns, qual=qual, pscrn=pscrn, device=device)
            eedi3 = partial(core.eedi3m.EEDI3CL, alpha=alpha, beta=beta, gamma=gamma, nrad=nrad, mdis=mdis, vcheck=vcheck, device=device)
        else:
            nnedi3 = partial(core.znedi3.nnedi3 if hasattr(core, 'znedi3') else core.nnedi3.nnedi3,
                             nsize=nsize, nns=nns, qual=qual, pscrn=pscrn, int16_prescreener=int16_prescreener, int16_predictor=int16_predictor, exp=exp)
            eedi3 = partial(core.eedi3m.EEDI3 if hasattr(core, 'eedi3m') else core.eedi3.eedi3,
                            alpha=alpha, beta=beta, gamma=gamma, nrad=nrad, mdis=mdis, vcheck=vcheck)

        strength = max(strength, 0)
        field = strength % 2
        dh = (strength <= 0 and not halfres)

        if strength > 0:
            c = santiag_stronger(c, strength - 1, type)

        w = c.width
        h = c.height

        if type == 'nnedi3':
            return nnedi3(c, field=field, dh=dh)
        elif type == 'eedi2':
            if not dh:
                cshift = 1 - field
                if c.format.color_family != vs.GRAY:
                    cshift = [cshift, cshift * (1 << c.format.subsampling_h)]
                c = Resize(c, w, h // 2, sy=cshift, kernel='point', dmode=1)
            return c.eedi2.EEDI2(field=field)
        elif type == 'eedi3':
            sclip = nnedi3(c, field=field, dh=dh)
            return eedi3(c, field=field, dh=dh, sclip=sclip)
        elif type == 'sangnom':
            if dh:
                cshift = -0.25
                if c.format.color_family != vs.GRAY:
                    cshift = [cshift, cshift * (1 << c.format.subsampling_h)]
                c = Resize(c, w, h * 2, sy=cshift, dmode=1)
            return c.sangnom.SangNom(order=field, aa=aa)
        else:
            raise vs.Error('santiag: unexpected value for type')

    if not isinstance(c, vs.VideoNode):
        raise vs.Error('santiag: This is not a clip')

    type = type.lower()
    typeh = type if typeh is None else typeh.lower()
    typev = type if typev is None else typev.lower()

    w = c.width
    h = c.height
    fwh = fw if strv < 0 else w
    fhh = fh if strv < 0 else h

    if strh >= 0:
        c = santiag_dir(c, strh, typeh, fwh, fhh)
    if strv >= 0:
        c = santiag_dir(c.std.Transpose(), strv, typev, fh, fw).std.Transpose()

    if fw is None:
        fw = w
    if fh is None:
        fh = h
    if strh < 0 and strv < 0:
        c = c.resize.Spline36(fw, fh)
    return c


# FixChromaBleedingMod v1.35
#
# Parameters:
#  cx (int)         - Horizontal chroma shift. Positive value shifts chroma to left, negative value shifts chroma to right. Default is 4
#  cy (int)         - Vertical chroma shift. Positive value shifts chroma upwards, negative value shifts chroma downwards. Default is 4
#  thr (float)      - Masking threshold, higher value treats more areas as color bleed. Default is 4.0
#  strength (float) - Saturation strength in clip to be merged with the original chroma. Value below 1.0 reduces the saturation, a value of 1.0 leaves the saturation intact. Default is 0.8
#  blur (bool)      - Set to true to blur the mask clip. Default is false
def FixChromaBleedingMod(input, cx=4, cy=4, thr=4.0, strength=0.8, blur=False):
    if not isinstance(input, vs.VideoNode):
        raise vs.Error('FixChromaBleedingMod: This is not a clip')

    if input.format.color_family in [vs.GRAY, vs.RGB]:
        raise vs.Error('FixChromaBleedingMod: Gray and RGB formats are not supported')

    neutral = 1 << (input.format.bits_per_sample - 1)
    peak = (1 << input.format.bits_per_sample) - 1

    def Levels(clip, input_low, gamma, input_high, output_low, output_high, coring=True):
        gamma = 1 / gamma
        divisor = input_high - input_low + (input_high == input_low)

        tvLow = scale(16, peak)
        tvHigh = [scale(235, peak), scale(240, peak)]
        scaleUp = peak / scale(219, peak)
        scaleDown = scale(219, peak) / peak

        def get_lut1(x):
            p = ((x - tvLow) * scaleUp - input_low) / divisor if coring else (x - input_low) / divisor
            p = min(max(p, 0), 1) ** gamma * (output_high - output_low) + output_low
            return min(max(cround(p * scaleDown + tvLow), tvLow), tvHigh[0]) if coring else min(max(cround(p), 0), peak)

        def get_lut2(x):
            q = cround((x - neutral) * (output_high - output_low) / divisor + neutral)
            return min(max(q, tvLow), tvHigh[1]) if coring else min(max(q, 0), peak)

        last = clip.std.Lut(planes=[0], function=get_lut1)
        if clip.format.color_family != vs.GRAY:
            last = last.std.Lut(planes=[1, 2], function=get_lut2)
        return last

    # prepare to work on the V channel and filter noise
    vch = mvf.GetPlane(adjust.Tweak(input, sat=thr), 2)
    if blur:
        area = vch.std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])
    else:
        area = vch

    # select and normalize both extremes of the scale
    red = Levels(area, scale(255, peak), 1.0, scale(255, peak), scale(255, peak), 0)
    blue = Levels(area, 0, 1.0, 0, 0, scale(255, peak))

    # merge both masks
    mask = core.std.Merge(red, blue)
    if not blur:
        mask = mask.std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])
    mask = Levels(mask, scale(250, peak), 1.0, scale(250, peak), scale(255, peak), 0)

    # expand to cover beyond the bleeding areas and shift to compensate the resizing
    mask = mask.std.Convolution(matrix=[0, 0, 0, 1, 0, 0, 0, 0, 0], divisor=1, saturate=False).std.Convolution(matrix=[1, 1, 1, 1, 1, 1, 0, 0, 0], divisor=8, saturate=False)

    # binarize (also a trick to expand)
    mask = Levels(mask, scale(10, peak), 1.0, scale(10, peak), 0, scale(255, peak)).std.Inflate()

    # prepare a version of the image that has its chroma shifted and less saturated
    input_c = adjust.Tweak(input.resize.Spline36(src_left=cx, src_top=cy), sat=strength)

    # combine both images using the mask
    fu = core.std.MaskedMerge(mvf.GetPlane(input, 1), mvf.GetPlane(input_c, 1), mask)
    fv = core.std.MaskedMerge(mvf.GetPlane(input, 2), mvf.GetPlane(input_c, 2), mask)
    return core.std.ShufflePlanes([input, fu, fv], planes=[0, 0, 0], colorfamily=input.format.color_family)


# Parameters:
#  quant1 (int) - Strength of block edge deblocking. Default is 24
#  quant2 (int) - Strength of block internal deblocking. Default is 26
#  aOff1 (int)  - Halfway "sensitivity" and halfway a strength modifier for borders. Default is 1
#  aOff2 (int)  - Halfway "sensitivity" and halfway a strength modifier for block interiors. Default is 1
#  bOff1 (int)  - "sensitivity to detect blocking" for borders. Default is 2
#  bOff2 (int)  - "sensitivity to detect blocking" for block interiors. Default is 2
#  uv (int)     - 3: use proposed method for chroma deblocking, 2: no chroma deblocking at all (fastest method), 1|-1: directly use chroma debl. from the normal|strong Deblock(). Default is 3
def Deblock_QED(clp, quant1=24, quant2=26, aOff1=1, bOff1=2, aOff2=1, bOff2=2, uv=3):
    if not isinstance(clp, vs.VideoNode):
        raise vs.Error('Deblock_QED: This is not a clip')

    isGray = (clp.format.color_family == vs.GRAY)
    planes = [0, 1, 2] if uv >= 3 and not isGray else [0]

    if clp.format.sample_type == vs.INTEGER:
        neutral = 1 << (clp.format.bits_per_sample - 1)
        peak = (1 << clp.format.bits_per_sample) - 1
    else:
        neutral = 0.0
        peak = 1.0

    # add borders if clp is not mod 8
    w = clp.width
    h = clp.height
    padX = 8 - w % 8 if w & 7 else 0
    padY = 8 - h % 8 if h & 7 else 0
    if padX or padY:
        clp = clp.resize.Point(w + padX, h + padY, src_width=w + padX, src_height=h + padY)

    # block
    block = clp.std.BlankClip(width=6, height=6, format=clp.format.replace(color_family=vs.GRAY, subsampling_w=0, subsampling_h=0), length=1, color=[0])
    block = block.std.AddBorders(1, 1, 1, 1, color=[peak])
    block = core.std.StackHorizontal([block for i in range(clp.width // 8)])
    block = core.std.StackVertical([block for i in range(clp.height // 8)])
    if not isGray:
        blockc = block.std.CropAbs(width=clp.width >> clp.format.subsampling_w, height=clp.height >> clp.format.subsampling_h)
        block = core.std.ShufflePlanes([block, blockc], planes=[0, 0, 0], colorfamily=clp.format.color_family)
    block = block.std.Loop(times=clp.num_frames)

    # create normal deblocking (for block borders) and strong deblocking (for block interiour)
    normal = clp.deblock.Deblock(quant=quant1, aoffset=aOff1, boffset=bOff1, planes=[0, 1, 2] if uv != 2 and not isGray else [0])
    strong = clp.deblock.Deblock(quant=quant2, aoffset=aOff2, boffset=bOff2, planes=[0, 1, 2] if uv != 2 and not isGray else [0])

    # build difference maps of both
    normalD = core.std.MakeDiff(clp, normal, planes=planes)
    strongD = core.std.MakeDiff(clp, strong, planes=planes)

    # separate border values of the difference maps, and set the interiours to '128'
    expr = f'y {peak} = x {neutral} ?'
    normalD2 = core.std.Expr([normalD, block], expr=[expr] if uv >= 3 or isGray else [expr, ''])
    strongD2 = core.std.Expr([strongD, block], expr=[expr] if uv >= 3 or isGray else [expr, ''])

    # interpolate the border values over the whole block: DCTFilter can do it. (Kiss to Tom Barry!)
    # (Note: this is not fully accurate, but a reasonable approximation.)
    # add borders if clp is not mod 16
    sw = strongD2.width
    sh = strongD2.height
    remX = 16 - sw % 16 if sw & 15 else 0
    remY = 16 - sh % 16 if sh & 15 else 0
    if remX or remY:
        strongD2 = strongD2.resize.Point(sw + remX, sh + remY, src_width=sw + remX, src_height=sh + remY)
    expr = f'x {neutral} - 1.01 * {neutral} +'
    strongD3 = strongD2.std.Expr(expr=[expr] if uv >= 3 or isGray else [expr, '']).dctf.DCTFilter(factors=[1, 1, 0, 0, 0, 0, 0, 0], planes=planes).std.Crop(right=remX, bottom=remY)

    # apply compensation from "normal" deblocking to the borders of the full-block-compensations calculated from "strong" deblocking ...
    expr = f'y {neutral} = x y ?'
    strongD4 = core.std.Expr([strongD3, normalD2], expr=[expr] if uv >= 3 or isGray else [expr, ''])

    # ... and apply it.
    deblocked = core.std.MakeDiff(clp, strongD4, planes=planes)

    # simple decisions how to treat chroma
    if not isGray:
        if uv <= -1:
            deblocked = core.std.ShufflePlanes([deblocked, strong], planes=[0, 1, 2], colorfamily=clp.format.color_family)
        elif uv <= 1:
            deblocked = core.std.ShufflePlanes([deblocked, normal], planes=[0, 1, 2], colorfamily=clp.format.color_family)

    # remove mod 8 borders
    return deblocked.std.Crop(right=padX, bottom=padY)


# rx, ry [float, 1.0 ... 2.0 ... ~3.0]
# As usual, the radii for halo removal.
# Note: this function is rather sensitive to the radius settings. Set it as low as possible! If radius is set too high, it will start missing small spots.
#
# darkkstr, brightstr [float, 0.0 ... 1.0] [<0.0 and >1.0 possible]
# The strength factors for processing dark and bright halos. Default 1.0 both for symmetrical processing.
# On Comic/Anime, darkstr=0.4~0.8 sometimes might be better ... sometimes. In General, the function seems to preserve dark lines rather good.
#
# lowsens, highsens [int, 0 ... 50 ... 100]
# Sensitivity settings, not that easy to describe them exactly ...
# In a sense, they define a window between how weak an achieved effect has to be to get fully accepted, and how strong an achieved effect has to be to get fully discarded.
# Defaults are 50 and 50 ... try and see for yourself.
#
# ss [float, 1.0 ... 1.5 ...]
# Supersampling factor, to avoid creation of aliasing.
def DeHalo_alpha(clp, rx=2.0, ry=2.0, darkstr=1.0, brightstr=1.0, lowsens=50, highsens=50, ss=1.5):
    if not isinstance(clp, vs.VideoNode):
        raise vs.Error('DeHalo_alpha: This is not a clip')

    if clp.format.color_family == vs.RGB:
        raise vs.Error('DeHalo_alpha: RGB format is not supported')

    peak = (1 << clp.format.bits_per_sample) - 1 if clp.format.sample_type == vs.INTEGER else 1.0

    if clp.format.color_family != vs.GRAY:
        clp_orig = clp
        clp = mvf.GetPlane(clp, 0)
    else:
        clp_orig = None

    ox = clp.width
    oy = clp.height

    halos = clp.resize.Bicubic(m4(ox / rx), m4(oy / ry), filter_param_a=1/3, filter_param_b=1/3).resize.Bicubic(ox, oy, filter_param_a=1, filter_param_b=0)
    are = core.std.Expr([clp.std.Maximum(), clp.std.Minimum()], expr=['x y -'])
    ugly = core.std.Expr([halos.std.Maximum(), halos.std.Minimum()], expr=['x y -'])
    expr = f'y x - y y 0 = + / {peak} * {scale(lowsens, peak)} - y {scale(256, peak)} + {scale(512, peak)} / {highsens / 100} + *'
    so = core.std.Expr([ugly, are], expr=[expr])
    lets = core.std.MaskedMerge(halos, clp, so)
    if ss <= 1:
        remove = core.rgvs.Repair(clp, lets, mode=[1])
    else:
        remove = core.std.Expr([core.std.Expr([clp.resize.Lanczos(m4(ox * ss), m4(oy * ss)),
                                               lets.std.Maximum().resize.Bicubic(m4(ox * ss), m4(oy * ss), filter_param_a=1/3, filter_param_b=1/3)],
                                               expr=['x y min']),
                                lets.std.Minimum().resize.Bicubic(m4(ox * ss), m4(oy * ss), filter_param_a=1/3, filter_param_b=1/3)],
                               expr=['x y max']).resize.Lanczos(ox, oy)
    them = core.std.Expr([clp, remove], expr=[f'x y < x x y - {darkstr} * - x x y - {brightstr} * - ?'])

    if clp_orig is not None:
        them = core.std.ShufflePlanes([them, clp_orig], planes=[0, 1, 2], colorfamily=clp_orig.format.color_family)
    return them


# EdgeCleaner() v1.04 (03/04/2012)
# - a simple edge cleaning and weak dehaloing function
#
# Requirements:
# AWarpSharp2, RGVS (optional)
#
# Parameters:
# strength (float)      - specifies edge denoising strength (10)
# rep (boolean)         - actives Repair for the aWarpSharped clip (true)
# rmode (integer)       - specifies the Repair mode;
#                         1 is very mild and good for halos,
#                         16 and 18 are good for edge structure preserval on strong settings but keep more halos and edge noise,
#                         17 is similar to 16 but keeps much less haloing, other modes are not recommended (17)
# smode (integer)       - specifies what method will be used for finding small particles, ie stars;
#                         0 is disabled, 1 uses RemoveGrain (0)
# hot (boolean)         - specifies whether removal of hot pixels should take place (false)
def EdgeCleaner(c, strength=10, rep=True, rmode=17, smode=0, hot=False):
    if not isinstance(c, vs.VideoNode):
        raise vs.Error('EdgeCleaner: This is not a clip')

    if c.format.color_family == vs.RGB:
        raise vs.Error('EdgeCleaner: RGB format is not supported')

    peak = (1 << c.format.bits_per_sample) - 1

    if c.format.color_family != vs.GRAY:
        c_orig = c
        c = mvf.GetPlane(c, 0)
    else:
        c_orig = None

    if smode > 0:
        strength += 4

    main = Padding(c, 6, 6, 6, 6).warp.AWarpSharp2(blur=1, depth=cround(strength / 2)).std.Crop(6, 6, 6, 6)
    if rep:
        main = core.rgvs.Repair(main, c, mode=[rmode])

    mask = AvsPrewitt(c).std.Expr(expr=[f'x {scale(4, peak)} < 0 x {scale(32, peak)} > {peak} x ? ?']).std.Invert().std.Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1])

    final = core.std.MaskedMerge(c, main, mask)
    if hot:
        final = core.rgvs.Repair(final, c, mode=[2])
    if smode > 0:
        clean = c.rgvs.RemoveGrain(mode=[17])
        diff = core.std.MakeDiff(c, clean)
        expr = f'x {scale(4, peak)} < 0 x {scale(16, peak)} > {peak} x ? ?'
        mask = AvsPrewitt(diff.std.Levels(min_in=scale(40, peak), max_in=scale(168, peak), gamma=0.35).rgvs.RemoveGrain(mode=[7])).std.Expr(expr=[expr])
        final = core.std.MaskedMerge(final, c, mask)

    if c_orig is not None:
        final = core.std.ShufflePlanes([final, c_orig], planes=[0, 1, 2], colorfamily=c_orig.format.color_family)
    return final


# This program is free software. It comes without any warranty, to
# the extent permitted by applicable law. You can redistribute it
# and/or modify it under the terms of the Do What The Fuck You Want
# To Public License, Version 2, as published by Sam Hocevar. See
# http://sam.zoy.org/wtfpl/COPYING for more details.
def FineDehalo(src, rx=2.0, ry=None, thmi=80, thma=128, thlimi=50, thlima=100, darkstr=1.0, brightstr=1.0, showmask=0, contra=0.0, excl=True, edgeproc=0.0):
    if not isinstance(src, vs.VideoNode):
        raise vs.Error('FineDehalo: This is not a clip')

    if src.format.color_family == vs.RGB:
        raise vs.Error('FineDehalo: RGB format is not supported')

    isInteger = (src.format.sample_type == vs.INTEGER)

    peak = (1 << src.format.bits_per_sample) - 1 if isInteger else 1.0

    if src.format.color_family != vs.GRAY:
        src_orig = src
        src = mvf.GetPlane(src, 0)
    else:
        src_orig = None

    if ry is None:
        ry = rx

    rx_i = cround(rx)
    ry_i = cround(ry)

    ### Dehaloing ###

    dehaloed = DeHalo_alpha(src, rx=rx, ry=ry, darkstr=darkstr, brightstr=brightstr)

    # Contrasharpening
    if contra > 0:
        dehaloed = FineDehalo_contrasharp(dehaloed, src, contra)

    ### Main edges ###

    # Basic edge detection, thresholding will be applied later
    edges = AvsPrewitt(src)

    # Keeps only the sharpest edges (line edges)
    strong = edges.std.Expr(expr=[f'x {scale(thmi, peak)} - {thma - thmi} / 255 *' if isInteger else f'x {scale(thmi, peak)} - {thma - thmi} / 255 * 0 max 1 min'])

    # Extends them to include the potential halos
    large = mt_expand_multi(strong, sw=rx_i, sh=ry_i)

    ### Exclusion zones ###

    # When two edges are close from each other (both edges of a single line or multiple parallel color bands), the halo removal oversmoothes them or makes seriously bleed the bands,
    # producing annoying artifacts. Therefore we have to produce a mask to exclude these zones from the halo removal

    # Includes more edges than previously, but ignores simple details
    light = edges.std.Expr(expr=[f'x {scale(thlimi, peak)} - {thlima - thlimi} / 255 *' if isInteger else f'x {scale(thlimi, peak)} - {thlima - thlimi} / 255 * 0 max 1 min'])

    # To build the exclusion zone, we make grow the edge mask, then shrink it to its original shape
    # During the growing stage, close adjacent edge masks will join and merge, forming a solid area, which will remain solid even after the shrinking stage

    # Mask growing
    shrink = mt_expand_multi(light, mode='ellipse', sw=rx_i, sh=ry_i)

    # At this point, because the mask was made of a shades of grey, we may end up with large areas of dark grey after shrinking
    # To avoid this, we amplify and saturate the mask here (actually we could even binarize it)
    shrink = shrink.std.Expr(expr=['x 4 *' if isInteger else 'x 4 * 1 min'])

    # Mask shrinking
    shrink = mt_inpand_multi(shrink, mode='ellipse', sw=rx_i, sh=ry_i)

    # This mask is almost binary, which will produce distinct discontinuities once applied. Then we have to smooth it
    shrink = shrink.std.Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1]).std.Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1])

    ### Final mask building ###

    # Previous mask may be a bit weak on the pure edge side, so we ensure that the main edges are really excluded. We do not want them to be smoothed by the halo removal
    if excl:
        shr_med = core.std.Expr([strong, shrink], expr=['x y max'])
    else:
        shr_med = strong

    # Substracts masks and amplifies the difference to be sure we get 255 on the areas to be processed
    outside = core.std.Expr([large, shr_med], expr=['x y - 2 *' if isInteger else 'x y - 2 * 0 max 1 min'])

    # If edge processing is required, adds the edgemask
    if edgeproc > 0:
        outside = core.std.Expr([outside, strong], expr=[f'x y {edgeproc * 0.66} * +' if isInteger else f'x y {edgeproc * 0.66} * + 1 min'])

    # Smooth again and amplify to grow the mask a bit, otherwise the halo parts sticking to the edges could be missed
    outside = outside.std.Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1]).std.Expr(expr=['x 2 *' if isInteger else 'x 2 * 1 min'])

    ### Masking ###

    if showmask <= 0:
        last = core.std.MaskedMerge(src, dehaloed, outside)

    if src_orig is not None:
        if showmask <= 0:
            return core.std.ShufflePlanes([last, src_orig], planes=[0, 1, 2], colorfamily=src_orig.format.color_family)
        elif showmask == 1:
            return core.resize.Bicubic(outside, format=src_orig.format.id)
        elif showmask == 2:
            return core.resize.Bicubic(shrink, format=src_orig.format.id)
        elif showmask == 3:
            return core.resize.Bicubic(edges, format=src_orig.format.id)
        else:
            return core.resize.Bicubic(strong, format=src_orig.format.id)
    else:
        if showmask <= 0:
            return last
        elif showmask == 1:
            return outside
        elif showmask == 2:
            return shrink
        elif showmask == 3:
            return edges
        else:
            return strong

# level == 1.0 : normal contrasharp
def FineDehalo_contrasharp(dehaloed, src, level):
    if not (isinstance(dehaloed, vs.VideoNode) and isinstance(src, vs.VideoNode)):
        raise vs.Error('FineDehalo_contrasharp: This is not a clip')

    if dehaloed.format.color_family == vs.RGB:
        raise vs.Error('FineDehalo_contrasharp: RGB format is not supported')

    if dehaloed.format.id != src.format.id:
        raise vs.Error('FineDehalo_contrasharp: Clips must be the same format')

    neutral = 1 << (dehaloed.format.bits_per_sample - 1) if dehaloed.format.sample_type == vs.INTEGER else 0.0

    if dehaloed.format.color_family != vs.GRAY:
        dehaloed_orig = dehaloed
        dehaloed = mvf.GetPlane(dehaloed, 0)
        src = mvf.GetPlane(src, 0)
    else:
        dehaloed_orig = None

    bb = dehaloed.std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])
    bb2 = core.rgvs.Repair(bb, core.rgvs.Repair(bb, bb.ctmf.CTMF(radius=2), mode=[1]), mode=[1])
    xd = core.std.MakeDiff(bb, bb2)
    xd = xd.std.Expr(expr=[f'x {neutral} - 2.49 * {level} * {neutral} +'])
    xdd = core.std.Expr([xd, core.std.MakeDiff(src, dehaloed)], expr=[f'x {neutral} - y {neutral} - * 0 < {neutral} x {neutral} - abs y {neutral} - abs < x y ? ?'])
    last = core.std.MergeDiff(dehaloed, xdd)

    if dehaloed_orig is not None:
        last = core.std.ShufflePlanes([last, dehaloed_orig], planes=[0, 1, 2], colorfamily=dehaloed_orig.format.color_family)
    return last


# Try to remove 2nd order halos
def FineDehalo2(src, hconv=[-1, -2, 0, 0, 40, 0, 0, -2, -1], vconv=[-2, -1, 0, 0, 40, 0, 0, -1, -2], showmask=0):
    def grow_mask(mask, coordinates):
        mask = mask.std.Maximum(coordinates=coordinates).std.Minimum(coordinates=coordinates)
        mask_1 = mask.std.Maximum(coordinates=coordinates)
        mask_2 = mask_1.std.Maximum(coordinates=coordinates).std.Maximum(coordinates=coordinates)
        mask = core.std.Expr([mask_2, mask_1], expr=['x y -'])
        return mask.std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1]).std.Expr(expr=['x 1.8 *'])

    if not isinstance(src, vs.VideoNode):
        raise vs.Error('FineDehalo2: This is not a clip')

    if src.format.color_family == vs.RGB:
        raise vs.Error('FineDehalo2: RGB format is not supported')

    if src.format.color_family != vs.GRAY:
        src_orig = src
        src = mvf.GetPlane(src, 0)
    else:
        src_orig = None

    fix_h = src.std.Convolution(matrix=vconv, mode='v')
    fix_v = src.std.Convolution(matrix=hconv, mode='h')
    mask_h = src.std.Convolution(matrix=[1, 2, 1, 0, 0, 0, -1, -2, -1], divisor=4, saturate=False)
    mask_v = src.std.Convolution(matrix=[1, 0, -1, 2, 0, -2, 1, 0, -1], divisor=4, saturate=False)
    temp_h = core.std.Expr([mask_h, mask_v], expr=['x 3 * y -'])
    temp_v = core.std.Expr([mask_v, mask_h], expr=['x 3 * y -'])
    mask_h = grow_mask(temp_h, [0, 1, 0, 0, 0, 0, 1, 0])
    mask_v = grow_mask(temp_v, [0, 0, 0, 1, 1, 0, 0, 0])

    if showmask <= 0:
        last = core.std.MaskedMerge(src, fix_h, mask_h)
        last = core.std.MaskedMerge(last, fix_v, mask_v)
    else:
        last = core.std.Expr([mask_h, mask_v], expr=['x y max'])

    if src_orig is not None:
        if showmask <= 0:
            last = core.std.ShufflePlanes([last, src_orig], planes=[0, 1, 2], colorfamily=src_orig.format.color_family)
        else:
            last = last.resize.Bicubic(format=src_orig.format.id)
    return last


# Y'et A'nother H'alo R'educing script
#
# Parameters:
#  blur (int)  - "blur" parameter of AWarpSharp2. Default is 2
#  depth (int) - "depth" parameter of AWarpSharp2. Default is 32
def YAHR(clp, blur=2, depth=32):
    if not isinstance(clp, vs.VideoNode):
        raise vs.Error('YAHR: This is not a clip')

    if clp.format.color_family == vs.RGB:
        raise vs.Error('YAHR: RGB format is not supported')

    if clp.format.color_family != vs.GRAY:
        clp_orig = clp
        clp = mvf.GetPlane(clp, 0)
    else:
        clp_orig = None

    b1 = MinBlur(clp, r=2).std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])
    b1D = core.std.MakeDiff(clp, b1)
    w1 = Padding(clp, 6, 6, 6, 6).warp.AWarpSharp2(blur=blur, depth=depth).std.Crop(6, 6, 6, 6)
    w1b1 = MinBlur(w1, r=2).std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])
    w1b1D = core.std.MakeDiff(w1, w1b1)
    DD = core.rgvs.Repair(b1D, w1b1D, mode=[13])
    DD2 = core.std.MakeDiff(b1D, DD)
    last = core.std.MakeDiff(clp, DD2)

    if clp_orig is not None:
        last = core.std.ShufflePlanes([last, clp_orig], planes=[0, 1, 2], colorfamily=clp_orig.format.color_family)
    return last


######
###
### HQDering mod v1.8      by mawen1250      2014.03.22
###
### Requirements: Miscellaneous Filters, RGVS, CTMF
###
### Applies deringing by using a smart smoother near edges (where ringing occurs) only
###
### Parameters:
###  mrad (int)      - Expanding of edge mask, higher value means more aggressive processing. Default is 1
###  msmooth (int)   - Inflate of edge mask, smooth boundaries of mask. Default is 1
###  incedge (bool)  - Whether to include edge in ring mask, by default ring mask only include area near edges. Default is false
###  mthr (int)      - Threshold of sobel edge mask, lower value means more aggressive processing. Or define your own mask clip "ringmask". Default is 60
###                    But for strong ringing, lower value will treat some ringing as edge, which protects this ringing from being processed.
###  minp (int)      - Inpanding of sobel edge mask, higher value means more aggressive processing. Default is 1
###  nrmode (int)    - Kernel of dering - 1: MinBlur(radius=1), 2: MinBlur(radius=2), 3: MinBlur(radius=3). Or define your own smoothed clip "p". Default is 2 for HD / 1 for SD
###  sharp (int)     - Whether to use contra-sharpening to resharp deringed clip, 1-3 represents radius, 0 means no sharpening. Default is 1
###  drrep (int)     - Use repair for details retention, recommended values are 24/23/13/12/1. Default is 24
###  thr (float)     - The same meaning with "thr" in Dither_limit_dif16, valid value range is [0.0, 128.0]. Default is 12.0
###  elast (float)   - The same meaning with "elast" in Dither_limit_dif16, valid value range is [1.0, inf). Default is 2.0
###                    Larger "thr" will result in more pixels being taken from processed clip
###                    Larger "thr" will result in less pixels being taken from input clip
###                    Larger "elast" will result in more pixels being blended from processed&input clip, for smoother merging
###  darkthr (float) - Threshold for darker area near edges, set it lower if you think deringing destroys too much lines, etc. Default is thr/4
###                    When "darkthr" is not equal to "thr", "thr" limits darkening while "darkthr" limits brightening
###  planes (int[])  - Whether to process the corresponding plane. The other planes will be passed through unchanged. Default is [0]
###  show (bool)     - Whether to output mask clip instead of filtered clip. Default is false
###
######
def HQDeringmod(input, p=None, ringmask=None, mrad=1, msmooth=1, incedge=False, mthr=60, minp=1, nrmode=None, sharp=1, drrep=24, thr=12.0, elast=2.0, darkthr=None, planes=[0], show=False):
    if not isinstance(input, vs.VideoNode):
        raise vs.Error('HQDeringmod: This is not a clip')

    if input.format.color_family == vs.RGB:
        raise vs.Error('HQDeringmod: RGB format is not supported')

    if p is not None and (not isinstance(p, vs.VideoNode) or p.format.id != input.format.id):
        raise vs.Error("HQDeringmod: 'p' must be the same format as input")

    if ringmask is not None and not isinstance(ringmask, vs.VideoNode):
        raise vs.Error("HQDeringmod: 'ringmask' is not a clip")

    isGray = (input.format.color_family == vs.GRAY)

    neutral = 1 << (input.format.bits_per_sample - 1)
    peak = (1 << input.format.bits_per_sample) - 1

    if isinstance(planes, int):
        planes = [planes]

    if nrmode is None:
        nrmode = 2 if input.width > 1024 or input.height > 576 else 1
    if darkthr is None:
        darkthr = thr / 4

    # Kernel: Smoothing
    if p is None:
        p = MinBlur(input, r=nrmode, planes=planes)

    # Post-Process: Contra-Sharpening
    matrix1 = [1, 2, 1, 2, 4, 2, 1, 2, 1]
    matrix2 = [1, 1, 1, 1, 1, 1, 1, 1, 1]

    if sharp <= 0:
        sclp = p
    else:
        pre = p.std.Median(planes=planes)
        if sharp == 1:
            method = pre.std.Convolution(matrix=matrix1, planes=planes)
        elif sharp == 2:
            method = pre.std.Convolution(matrix=matrix1, planes=planes).std.Convolution(matrix=matrix2, planes=planes)
        else:
            method = pre.std.Convolution(matrix=matrix1, planes=planes).std.Convolution(matrix=matrix2, planes=planes).std.Convolution(matrix=matrix2, planes=planes)
        sharpdiff = core.std.MakeDiff(pre, method, planes=planes)
        allD = core.std.MakeDiff(input, p, planes=planes)
        ssDD = core.rgvs.Repair(sharpdiff, allD, mode=[1 if i in planes else 0 for i in range(input.format.num_planes)])
        expr = f'x {neutral} - abs y {neutral} - abs <= x y ?'
        ssDD = core.std.Expr([ssDD, sharpdiff], expr=[expr if i in planes else '' for i in range(input.format.num_planes)])
        sclp = core.std.MergeDiff(p, ssDD, planes=planes)

    # Post-Process: Repairing
    if drrep <= 0:
        repclp = sclp
    else:
        repclp = core.rgvs.Repair(input, sclp, mode=[drrep if i in planes else 0 for i in range(input.format.num_planes)])

    # Post-Process: Limiting
    if (thr <= 0 and darkthr <= 0) or (thr >= 128 and darkthr >= 128):
        limitclp = repclp
    else:
        limitclp = mvf.LimitFilter(repclp, input, thr=thr, elast=elast, brighten_thr=darkthr, planes=planes)

    # Post-Process: Ringing Mask Generating
    if ringmask is None:
        expr = f'x {scale(mthr, peak)} < 0 x ?'
        prewittm = AvsPrewitt(input, planes=[0]).std.Expr(expr=[expr] if isGray else [expr, ''])
        fmask = core.misc.Hysteresis(prewittm.std.Median(planes=[0]), prewittm, planes=[0])
        if mrad > 0:
            omask = mt_expand_multi(fmask, planes=[0], sw=mrad, sh=mrad)
        else:
            omask = fmask
        if msmooth > 0:
            omask = mt_inflate_multi(omask, planes=[0], radius=msmooth)
        if incedge:
            ringmask = omask
        else:
            if minp > 3:
                imask = fmask.std.Minimum(planes=[0]).std.Minimum(planes=[0])
            elif minp > 2:
                imask = fmask.std.Inflate(planes=[0]).std.Minimum(planes=[0]).std.Minimum(planes=[0])
            elif minp > 1:
                imask = fmask.std.Minimum(planes=[0])
            elif minp > 0:
                imask = fmask.std.Inflate(planes=[0]).std.Minimum(planes=[0])
            else:
                imask = fmask
            expr = f'x {peak} y - * {peak} /'
            ringmask = core.std.Expr([omask, imask], expr=[expr] if isGray else [expr, ''])

    # Mask Merging & Output
    if show:
        if isGray:
            return ringmask
        else:
            return ringmask.std.Expr(expr=['', repr(neutral)])
    else:
        return core.std.MaskedMerge(input, limitclp, ringmask, planes=planes, first_plane=True)


#-------------------------------------------------------------------#
#                                                                   #
#                    QTGMC 3.33, by Vit, 2012                       #
#                                                                   #
#   A high quality deinterlacer using motion-compensated temporal   #
#  smoothing, with a range of features for quality and convenience  #
#          Originally based on TempGaussMC_beta2 by Didée           #
#                                                                   #
#-------------------------------------------------------------------#
#
# Full documentation is in the 'QTGMC' html file that comes with this script
#
# --- LATEST CHANGES ---
#
# v3.33
# - Increased maximum value for Rep0, Rep1 and Rep2 to 7 (from 5). Higher values help with flicker on static detail, potential for minor motion blur
# - Bug fix for the fact that Bob always outputs a BFF clip regardless of field order of input (thanks ajp_anton)
# - Improved generation of noise (NoiseDeint="Generate") for noise bypass / EZKeepGrain
# - Minor change to denoising
#
# --- REQUIREMENTS ---
#
# Core plugins:
#   MVTools
#   Miscellaneous Filters
#   znedi3
#   RGVS
#   fmtconv
#
# Additional plugins:
#   eedi3m - if selected directly or via a source-match preset
#   FFT3DFilter - if selected for noise processing
#   DFTTest - if selected for noise processing
#       For FFT3DFilter & DFTTest you also need the FFTW3 library (FFTW.org). On Windows the file needed for both is libfftw3f-3.dll.
#       Put the file in your System32 or SysWow64 folder
#   KNLMeansCL - if selected for noise processing
#   AddGrain - if NoiseDeint="Generate" selected for noise bypass
#
# --- GETTING STARTED ---
#
# The "Preset" used selects sensible settings for a given encoding speed. Choose a preset from:
#   "Placebo", "Very Slow", "Slower", "Slow", "Medium", "Fast", "Faster", "Very Fast", "Super Fast", "Ultra Fast" & "Draft"
# The default preset is "Slower"
# Don't be obsessed with using slower settings as the differences can be small. HD material benefits little from extreme settings (and will be very slow)
# For much faster speeds read the full documentation, the section on 'Multi-threading'
#
# There are many settings for tweaking the script, full details in the main documentation. You can display settings currently being used with "ShowSettings":
#   QTGMC( Preset="Slow", ShowSettings=True )
def QTGMC(Input, Preset='Slower', TR0=None, TR1=None, TR2=None, Rep0=None, Rep1=0, Rep2=None, EdiMode=None, RepChroma=True, NNSize=None, NNeurons=None, EdiQual=1, EdiMaxD=None, ChromaEdi='',
          EdiExt=None, Sharpness=None, SMode=None, SLMode=None, SLRad=None, SOvs=0, SVThin=0.0, Sbb=None, SrchClipPP=None, SubPel=None, SubPelInterp=2, BlockSize=None, Overlap=None, Search=None,
          SearchParam=None, PelSearch=None, ChromaMotion=None, TrueMotion=False, Lambda=None, LSAD=None, PNew=None, PLevel=None, GlobalMotion=True, DCT=0, ThSAD1=640, ThSAD2=256, ThSCD1=180, ThSCD2=98,
          SourceMatch=0, MatchPreset=None, MatchEdi=None, MatchPreset2=None, MatchEdi2=None, MatchTR2=1, MatchEnhance=0.5, Lossless=0, NoiseProcess=None, EZDenoise=None, EZKeepGrain=None,
          NoisePreset='Fast', Denoiser=None, FftThreads=1, DenoiseMC=None, NoiseTR=None, Sigma=None, ChromaNoise=False, ShowNoise=0.0, GrainRestore=None, NoiseRestore=None, NoiseDeint=None,
          StabilizeNoise=None, InputType=0, ProgSADMask=None, FPSDivisor=1, ShutterBlur=0, ShutterAngleSrc=180, ShutterAngleOut=180, SBlurLimit=4, Border=False, Precise=None, Tuning='None',
          ShowSettings=False, ForceTR=0, TFF=None, pscrn=None, int16_prescreener=None, int16_predictor=None, exp=None, alpha=None, beta=None, gamma=None, nrad=None, vcheck=None, opencl=False, device=None):
    #---------------------------------------
    # Presets

    # Select presets / tuning
    Preset = Preset.lower()
    presets = ['placebo', 'very slow', 'slower', 'slow', 'medium', 'fast', 'faster', 'very fast', 'super fast', 'ultra fast', 'draft']
    try:
        pNum = presets.index(Preset)
    except:
        raise vs.Error("QTGMC: 'Preset' choice is invalid")

    if MatchPreset is None:
        mpNum1 = pNum + 3 if pNum + 3 <= 9 else 9
        MatchPreset = presets[mpNum1]
    else:
        try:
            mpNum1 = presets[:10].index(MatchPreset.lower())
        except:
            raise vs.Error("QTGMC: 'MatchPreset' choice is invalid/unsupported")

    if MatchPreset2 is None:
        mpNum2 = mpNum1 + 2 if mpNum1 + 2 <= 9 else 9
        MatchPreset2 = presets[mpNum2]
    else:
        try:
            mpNum2 = presets[:10].index(MatchPreset2.lower())
        except:
            raise vs.Error("QTGMC: 'MatchPreset2' choice is invalid/unsupported")

    try:
        npNum = presets[2:7].index(NoisePreset.lower())
    except:
        raise vs.Error("QTGMC: 'NoisePreset' choice is invalid")

    try:
        tNum = ['none', 'dv-sd', 'dv-hd'].index(Tuning.lower())
    except:
        raise vs.Error("QTGMC: 'Tuning' choice is invalid")

    # Tunings only affect blocksize in this version
    bs = [16, 16, 32][tNum]
    bs2 = 32 if bs >= 16 else bs * 2

    #                                                   Very                                                        Very      Super     Ultra
    # Preset groups:                          Placebo   Slow      Slower    Slow      Medium    Fast      Faster    Fast      Fast      Fast      Draft
    if TR0          is None: TR0          = [ 2,        2,        2,        2,        2,        2,        1,        1,        1,        1,        0      ][pNum]
    if TR1          is None: TR1          = [ 2,        2,        2,        1,        1,        1,        1,        1,        1,        1,        1      ][pNum]
    if TR2 is not None:
        TR2X = TR2
    else:
        TR2X                              = [ 3,        2,        1,        1,        1,        0,        0,        0,        0,        0,        0      ][pNum]
    if Rep0         is None: Rep0         = [ 4,        4,        4,        4,        3,        3,        0,        0,        0,        0,        0      ][pNum]
    if Rep2         is None: Rep2         = [ 4,        4,        4,        4,        4,        4,        4,        4,        3,        3,        0      ][pNum]
    if EdiMode is not None:
        EdiMode = EdiMode.lower()
    else:
        EdiMode                           = ['nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', 'bob'   ][pNum]
    if NNSize       is None: NNSize       = [ 1,        1,        1,        1,        5,        5,        4,        4,        4,        4,        4      ][pNum]
    if NNeurons     is None: NNeurons     = [ 2,        2,        1,        1,        1,        0,        0,        0,        0,        0,        0      ][pNum]
    if EdiMaxD      is None: EdiMaxD      = [ 12,       10,       8,        7,        7,        6,        6,        5,        4,        4,        4      ][pNum]
    ChromaEdi = ChromaEdi.lower()
    if SMode        is None: SMode        = [ 2,        2,        2,        2,        2,        2,        2,        2,        2,        2,        0      ][pNum]
    if SLMode is not None:
        SLModeX = SLMode
    else:
        SLModeX                           = [ 2,        2,        2,        2,        2,        2,        2,        2,        0,        0,        0      ][pNum]
    if SLRad        is None: SLRad        = [ 3,        1,        1,        1,        1,        1,        1,        1,        1,        1,        1      ][pNum]
    if Sbb          is None: Sbb          = [ 3,        1,        1,        0,        0,        0,        0,        0,        0,        0,        0      ][pNum]
    if SrchClipPP   is None: SrchClipPP   = [ 3,        3,        3,        3,        3,        2,        2,        2,        1,        1,        0      ][pNum]
    if SubPel       is None: SubPel       = [ 2,        2,        2,        2,        1,        1,        1,        1,        1,        1,        1      ][pNum]
    if BlockSize    is None: BlockSize    = [ bs,       bs,       bs,       bs,       bs,       bs,       bs2,      bs2,      bs2,      bs2,      bs2    ][pNum]
    bs = BlockSize
    if Overlap      is None: Overlap      = [ bs // 2,  bs // 2,  bs // 2,  bs // 2,  bs // 2,  bs // 2,  bs // 2,  bs // 4,  bs // 4,  bs // 4,  bs // 4][pNum]
    if Search       is None: Search       = [ 5,        4,        4,        4,        4,        4,        4,        4,        0,        0,        0      ][pNum]
    if SearchParam  is None: SearchParam  = [ 2,        2,        2,        2,        2,        2,        2,        1,        1,        1,        1      ][pNum]
    if PelSearch    is None: PelSearch    = [ 2,        2,        2,        2,        1,        1,        1,        1,        1,        1,        1      ][pNum]
    if ChromaMotion is None: ChromaMotion = [ True,     True,     True,     False,    False,    False,    False,    False,    False,    False,    False  ][pNum]
    if Precise      is None: Precise      = [ True,     True,     False,    False,    False,    False,    False,    False,    False,    False,    False  ][pNum]
    if ProgSADMask  is None: ProgSADMask  = [ 10.0,     10.0,     10.0,     10.0,     10.0,     0.0,      0.0,      0.0,      0.0,      0.0,      0.0    ][pNum]

    # Noise presets                               Slower      Slow       Medium     Fast      Faster
    if Denoiser is not None:
        Denoiser = Denoiser.lower()
    else:
        Denoiser                              = ['dfttest',  'dfttest', 'dfttest', 'fft3df', 'fft3df'][npNum]
    if DenoiseMC      is None: DenoiseMC      = [ True,       True,      False,     False,    False  ][npNum]
    if NoiseTR        is None: NoiseTR        = [ 2,          1,         1,         1,        0      ][npNum]
    if NoiseDeint is not None:
        NoiseDeint = NoiseDeint.lower()
    else:
        NoiseDeint                            = ['generate', 'bob',      '',        '',       ''     ][npNum]
    if StabilizeNoise is None: StabilizeNoise = [ True,       True,      True,      False,    False  ][npNum]

    # The basic source-match step corrects and re-runs the interpolation of the input clip. So it initialy uses same interpolation settings as the main preset
    MatchNNSize = NNSize
    MatchNNeurons = NNeurons
    MatchEdiQual = EdiQual
    MatchEdiMaxD = EdiMaxD

    # However, can use a faster initial interpolation when using source-match allowing the basic source-match step to "correct" it with higher quality settings
    if SourceMatch > 0 and mpNum1 < pNum:
        raise vs.Error("QTGMC: 'MatchPreset' cannot use a slower setting than 'Preset'")
    # Basic source-match presets
    if SourceMatch > 0:
        #                     Very                                            Very   Super   Ultra
        #           Placebo   Slow   Slower   Slow   Medium   Fast   Faster   Fast   Fast    Fast
        NNSize   = [1,        1,     1,       1,     5,       5,     4,       4,     4,      4][mpNum1]
        NNeurons = [2,        2,     1,       1,     1,       0,     0,       0,     0,      0][mpNum1]
        EdiQual  = [1,        1,     1,       1,     1,       1,     1,       1,     1,      1][mpNum1]
        EdiMaxD  = [12,       10,    8,       7,     7,       6,     6,       5,     4,      4][mpNum1]
    TempEdi = EdiMode # Main interpolation is actually done by basic-source match step when enabled, so a little swap and wriggle is needed
    if SourceMatch > 0 and MatchEdi is not None:
        EdiMode = MatchEdi.lower()
    MatchEdi = TempEdi

    #                                             Very                                                        Very      Super    Ultra
    # Refined source-match presets      Placebo   Slow      Slower    Slow      Medium    Fast      Faster    Fast      Fast     Fast
    if MatchEdi2 is not None:
        MatchEdi2 = MatchEdi2.lower()
    else:
        MatchEdi2                   = ['nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', 'nnedi3', ''][mpNum2]
    MatchNNSize2                    = [ 1,        1,        1,        1,        5,        5,        4,        4,        4,       4 ][mpNum2]
    MatchNNeurons2                  = [ 2,        2,        1,        1,        1,        0,        0,        0,        0,       0 ][mpNum2]
    MatchEdiQual2                   = [ 1,        1,        1,        1,        1,        1,        1,        1,        1,       1 ][mpNum2]
    MatchEdiMaxD2                   = [ 12,       10,       8,        7,        7,        6,        6,        5,        4,       4 ][mpNum2]

    #---------------------------------------
    # Settings

    if not isinstance(Input, vs.VideoNode):
        raise vs.Error('QTGMC: This is not a clip')

    if EdiExt is not None and (not isinstance(EdiExt, vs.VideoNode) or EdiExt.format.id != Input.format.id):
        raise vs.Error("QTGMC: 'EdiExt' must be the same format as input")

    if InputType != 1 and not isinstance(TFF, bool):
        raise vs.Error("QTGMC: 'TFF' must be set when InputType!=1. Setting TFF to true means top field first and false means bottom field first")

    isGray = (Input.format.color_family == vs.GRAY)

    neutral = 1 << (Input.format.bits_per_sample - 1)
    peak = (1 << Input.format.bits_per_sample) - 1

    SOvs = scale(SOvs, peak)

    # Core defaults
    if SourceMatch > 0:
        if TR2 is None:
            TR2 = 1 if TR2X <= 0 else TR2X # ***TR2 defaults always at least 1 when using source-match***
    else:
        TR2 = TR2X

    # Source-match defaults
    MatchTR1 = TR1

    # Sharpness defaults. Sharpness default is always 1.0 (0.2 with source-match), but adjusted to give roughly same sharpness for all settings
    if Sharpness is not None and Sharpness <= 0:
        SMode = 0
    if SourceMatch > 0:
        if SLMode is None:
            SLMode = 0 # ***Sharpness limiting disabled by default for source-match***
    else:
        SLMode = SLModeX
    if SLRad <= 0:
        SLMode = 0
    spatialSL = SLMode in [1, 3]
    temporalSL = SLMode in [2, 4]
    if Sharpness is None:
        Sharpness = 0.0 if SMode <= 0 else 0.2 if SourceMatch > 0 else 1.0 # Default sharpness is 1.0, or 0.2 if using source-match
    sharpMul = 2 if temporalSL else 1.5 if spatialSL else 1 # Adjust sharpness based on other settings
    sharpAdj = Sharpness * (sharpMul * (0.2 + TR1 * 0.15 + TR2 * 0.25) + (0.1 if SMode == 1 else 0)) # [This needs a bit more refinement]
    if SMode <= 0:
        Sbb = 0

    # Noise processing settings
    if EZDenoise is not None and EZDenoise > 0 and EZKeepGrain is not None and EZKeepGrain > 0:
        raise vs.Error("QTGMC: EZDenoise and EZKeepGrain cannot be used together")
    if NoiseProcess is None:
        if EZDenoise is not None and EZDenoise > 0:
            NoiseProcess = 1
        elif (EZKeepGrain is not None and EZKeepGrain > 0) or Preset in ['placebo', 'very slow']:
            NoiseProcess = 2
        else:
            NoiseProcess = 0
    if GrainRestore is None:
        if EZDenoise is not None and EZDenoise > 0:
            GrainRestore = 0.0
        elif EZKeepGrain is not None and EZKeepGrain > 0:
            GrainRestore = 0.3 * math.sqrt(EZKeepGrain)
        else:
            GrainRestore = [0.0, 0.7, 0.3][NoiseProcess]
    if NoiseRestore is None:
        if EZDenoise is not None and EZDenoise > 0:
            NoiseRestore = 0.0
        elif EZKeepGrain is not None and EZKeepGrain > 0:
            NoiseRestore = 0.1 * math.sqrt(EZKeepGrain)
        else:
            NoiseRestore = [0.0, 0.3, 0.1][NoiseProcess]
    if Sigma is None:
        if EZDenoise is not None and EZDenoise > 0:
            Sigma = EZDenoise
        elif EZKeepGrain is not None and EZKeepGrain > 0:
            Sigma = 4.0 * EZKeepGrain
        else:
            Sigma = 2.0
    if isinstance(ShowNoise, bool):
        ShowNoise = 10.0 if ShowNoise else 0.0
    if ShowNoise > 0:
        NoiseProcess = 2
        NoiseRestore = 1.0
    if NoiseProcess <= 0:
        NoiseTR = 0
        GrainRestore = 0.0
        NoiseRestore = 0.0
    totalRestore = GrainRestore + NoiseRestore
    if totalRestore <= 0:
        StabilizeNoise = False
    noiseTD = [1, 3, 5][NoiseTR]
    noiseCentre = 128.5 * 2 ** (Input.format.bits_per_sample - 8) if Denoiser in ['fft3df', 'fft3dfilter'] else neutral

    # MVTools settings
    if Lambda is None:
        Lambda = (1000 if TrueMotion else 100) * BlockSize * BlockSize // 64
    if LSAD is None:
        LSAD = 1200 if TrueMotion else 400
    if PNew is None:
        PNew = 50 if TrueMotion else 25
    if PLevel is None:
        PLevel = 1 if TrueMotion else 0

    # Motion blur settings
    if ShutterAngleOut * FPSDivisor == ShutterAngleSrc:
        ShutterBlur = 0 # If motion blur output is same as input

    # Miscellaneous
    if InputType < 2:
        ProgSADMask = 0.0

    # Get maximum temporal radius needed
    maxTR = SLRad if temporalSL else 0
    maxTR = max(maxTR, MatchTR2, TR1, TR2, NoiseTR)
    if (ProgSADMask > 0 or StabilizeNoise or ShutterBlur > 0) and maxTR < 1:
        maxTR = 1
    maxTR = max(maxTR, ForceTR)

    #---------------------------------------
    # Pre-Processing

    w = Input.width
    h = Input.height
    epsilon = 1e-6

    # Reverse "field" dominance for progressive repair mode 3 (only difference from mode 2)
    if InputType >= 3:
        TFF = not TFF

    # Pad vertically during processing (to prevent artefacts at top & bottom edges)
    if Border:
        h += 8
        clip = Input.resize.Point(w, h, src_top=-4, src_height=h)
    else:
        clip = Input

    hpad = BlockSize
    vpad = BlockSize

    #---------------------------------------
    # Motion Analysis

    # Bob the input as a starting point for motion search clip
    if InputType <= 0:
        bobbed = Bob(clip, 0, 0.5, TFF)
    elif InputType == 1:
        bobbed = clip
    else:
        bobbed = clip.std.Convolution(matrix=[1, 2, 1], mode='v')

    CMplanes = [0, 1, 2] if ChromaMotion and not isGray else [0]

    # The bobbed clip will shimmer due to being derived from alternating fields. Temporally smooth over the neighboring frames using a binomial kernel. Binomial
    # kernels give equal weight to even and odd frames and hence average away the shimmer. The two kernels used are [1 2 1] and [1 4 6 4 1] for radius 1 and 2.
    # These kernels are approximately Gaussian kernels, which work well as a prefilter before motion analysis (hence the original name for this script)
    # Create linear weightings of neighbors first                                                     -2    -1    0     1     2
    if TR0 > 0: ts1 = AverageFrames(bobbed, weights=[1] * 3, scenechange=28 / 255, planes=CMplanes) # 0.00  0.33  0.33  0.33  0.00
    if TR0 > 1: ts2 = AverageFrames(bobbed, weights=[1] * 5, scenechange=28 / 255, planes=CMplanes) # 0.20  0.20  0.20  0.20  0.20

    # Combine linear weightings to give binomial weightings - TR0=0: (1), TR0=1: (1:2:1), TR0=2: (1:4:6:4:1)
    if TR0 <= 0:
        binomial0 = bobbed
    elif TR0 == 1:
        binomial0 = core.std.Merge(ts1, bobbed, weight=[0.25] if ChromaMotion or isGray else [0.25, 0])
    else:
        binomial0 = core.std.Merge(core.std.Merge(ts1, ts2, weight=[0.357] if ChromaMotion or isGray else [0.357, 0]), bobbed, weight=[0.125] if ChromaMotion or isGray else [0.125, 0])

    # Remove areas of difference between temporal blurred motion search clip and bob that are not due to bob-shimmer - removes general motion blur
    if Rep0 <= 0:
        repair0 = binomial0
    else:
        repair0 = QTGMC_KeepOnlyBobShimmerFixes(binomial0, bobbed, Rep0, RepChroma and ChromaMotion)

    # Blur image and soften edges to assist in motion matching of edge blocks. Blocks are matched by SAD (sum of absolute differences between blocks), but even
    # a slight change in an edge from frame to frame will give a high SAD due to the higher contrast of edges
    if SrchClipPP == 1:
        spatialBlur = repair0.resize.Bilinear(w // 2, h // 2).std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1], planes=CMplanes).resize.Bilinear(w, h)
    elif SrchClipPP >= 2:
        spatialBlur = Resize(repair0.std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1], planes=CMplanes), w, h, sw=w + epsilon, sh=h + epsilon, kernel='gauss', a1=2, dmode=1)
    if SrchClipPP > 1:
        spatialBlur = core.std.Merge(spatialBlur, repair0, weight=[0.1] if ChromaMotion or isGray else [0.1, 0])
    if SrchClipPP <= 0:
        srchClip = repair0
    elif SrchClipPP < 3:
        srchClip = spatialBlur
    else:
        expr = 'x {i} + y < x {i} + x {i} - y > x {i} - y ? ?'.format(i=scale(3, peak))
        tweaked = core.std.Expr([repair0, bobbed], expr=[expr] if ChromaMotion or isGray else [expr, ''])
        expr = 'x {i} + y < x {j} + x {i} - y > x {j} - x 51 * y 49 * + 100 / ? ?'.format(i=scale(7, peak), j=scale(2, peak))
        srchClip = core.std.Expr([spatialBlur, tweaked], expr=[expr] if ChromaMotion or isGray else [expr, ''])

    # Calculate forward and backward motion vectors from motion search clip
    if maxTR > 0:
        analyse_args = dict(blksize=BlockSize, overlap=Overlap, search=Search, searchparam=SearchParam, pelsearch=PelSearch, truemotion=TrueMotion, _lambda=Lambda, lsad=LSAD, pnew=PNew, plevel=PLevel,
                            _global=GlobalMotion, dct=DCT, chroma=ChromaMotion)
        srchSuper = DitherLumaRebuild(srchClip, s0=1, chroma=ChromaMotion).mv.Super(pel=SubPel, sharp=SubPelInterp, hpad=hpad, vpad=vpad, chroma=ChromaMotion)
        bVec1 = srchSuper.mv.Analyse(isb=True, delta=1, **analyse_args)
        fVec1 = srchSuper.mv.Analyse(isb=False, delta=1, **analyse_args)
    if maxTR > 1:
        bVec2 = srchSuper.mv.Analyse(isb=True, delta=2, **analyse_args)
        fVec2 = srchSuper.mv.Analyse(isb=False, delta=2, **analyse_args)
    if maxTR > 2:
        bVec3 = srchSuper.mv.Analyse(isb=True, delta=3, **analyse_args)
        fVec3 = srchSuper.mv.Analyse(isb=False, delta=3, **analyse_args)

    #---------------------------------------
    # Noise Processing

    # Expand fields to full frame size before extracting noise (allows use of motion vectors which are frame-sized)
    if NoiseProcess > 0:
        if InputType > 0:
            fullClip = clip
        else:
            fullClip = Bob(clip, 0, 1, TFF)
    if NoiseTR > 0:
        fullSuper = fullClip.mv.Super(pel=SubPel, levels=1, hpad=hpad, vpad=vpad, chroma=ChromaNoise) #TEST chroma OK?

    CNplanes = [0, 1, 2] if ChromaNoise and not isGray else [0]

    # Create a motion compensated temporal window around current frame and use to guide denoisers
    if NoiseProcess > 0:
        if not DenoiseMC or NoiseTR <= 0:
            noiseWindow = fullClip
        elif NoiseTR == 1:
            noiseWindow = core.std.Interleave([core.mv.Compensate(fullClip, fullSuper, fVec1, thscd1=ThSCD1, thscd2=ThSCD2),
                                               fullClip,
                                               core.mv.Compensate(fullClip, fullSuper, bVec1, thscd1=ThSCD1, thscd2=ThSCD2)])
        else:
            noiseWindow = core.std.Interleave([core.mv.Compensate(fullClip, fullSuper, fVec2, thscd1=ThSCD1, thscd2=ThSCD2),
                                               core.mv.Compensate(fullClip, fullSuper, fVec1, thscd1=ThSCD1, thscd2=ThSCD2),
                                               fullClip,
                                               core.mv.Compensate(fullClip, fullSuper, bVec1, thscd1=ThSCD1, thscd2=ThSCD2),
                                               core.mv.Compensate(fullClip, fullSuper, bVec2, thscd1=ThSCD1, thscd2=ThSCD2)])
        if Denoiser == 'dfttest':
            dnWindow = noiseWindow.dfttest.DFTTest(sigma=Sigma * 4, tbsize=noiseTD, planes=CNplanes)
        elif Denoiser == 'knlmeanscl':
            if ChromaNoise and not isGray:
                dnWindow = KNLMeansCL(noiseWindow, d=NoiseTR, h=Sigma)
            else:
                dnWindow = noiseWindow.knlm.KNLMeansCL(d=NoiseTR, h=Sigma)
        else:
            dnWindow = noiseWindow.fft3dfilter.FFT3DFilter(sigma=Sigma, planes=CNplanes, bt=noiseTD, ncpu=FftThreads)

        # Rework denoised clip to match source format - various code paths here: discard the motion compensation window, discard doubled lines (from point resize)
        # Also reweave to get interlaced noise if source was interlaced (could keep the full frame of noise, but it will be poor quality from the point resize)
        if not DenoiseMC:
            if InputType > 0:
                denoised = dnWindow
            else:
                denoised = Weave(dnWindow.std.SeparateFields(tff=TFF).std.SelectEvery(cycle=4, offsets=[0, 3]), tff=TFF)
        elif InputType > 0:
            if NoiseTR <= 0:
                denoised = dnWindow
            else:
                denoised = dnWindow.std.SelectEvery(cycle=noiseTD, offsets=[NoiseTR])
        else:
            denoised = Weave(dnWindow.std.SeparateFields(tff=TFF).std.SelectEvery(cycle=noiseTD * 4, offsets=[NoiseTR * 2, NoiseTR * 6 + 3]), tff=TFF)

    # Get actual noise from difference. Then 'deinterlace' where we have weaved noise - create the missing lines of noise in various ways
    if NoiseProcess > 0 and totalRestore > 0:
        noise = core.std.MakeDiff(clip, denoised, planes=CNplanes)
        if InputType > 0:
            deintNoise = noise
        elif NoiseDeint == 'bob':
            deintNoise = Bob(noise, 0, 0.5, TFF)
        elif NoiseDeint == 'generate':
            deintNoise = QTGMC_Generate2ndFieldNoise(noise, denoised, ChromaNoise, TFF)
        else:
            deintNoise = noise.std.SeparateFields(tff=TFF).std.DoubleWeave(tff=TFF)

        # Motion-compensated stabilization of generated noise
        if StabilizeNoise:
            noiseSuper = deintNoise.mv.Super(pel=SubPel, sharp=SubPelInterp, levels=1, hpad=hpad, vpad=vpad, chroma=ChromaNoise)
            mcNoise = core.mv.Compensate(deintNoise, noiseSuper, bVec1, thscd1=ThSCD1, thscd2=ThSCD2)
            expr = f'x {neutral} - abs y {neutral} - abs > x y ? 0.6 * x y + 0.2 * +'
            finalNoise = core.std.Expr([deintNoise, mcNoise], expr=[expr] if ChromaNoise or isGray else [expr, ''])
        else:
            finalNoise = deintNoise

    # If NoiseProcess=1 denoise input clip. If NoiseProcess=2 leave noise in the clip and let the temporal blurs "denoise" it for a stronger effect
    innerClip = denoised if NoiseProcess == 1 else clip

    #---------------------------------------
    # Interpolation

    # Support badly deinterlaced progressive content - drop half the fields and reweave to get 1/2fps interlaced stream appropriate for QTGMC processing
    if InputType > 1:
        ediInput = Weave(innerClip.std.SeparateFields(tff=TFF).std.SelectEvery(cycle=4, offsets=[0, 3]), tff=TFF)
    else:
        ediInput = innerClip

    # Create interpolated image as starting point for output
    if EdiExt is not None:
        edi1 = EdiExt.resize.Point(w, h, src_top=(EdiExt.height - h) // 2, src_height=h)
    else:
        edi1 = QTGMC_Interpolate(ediInput, InputType, EdiMode, NNSize, NNeurons, EdiQual, EdiMaxD, pscrn, int16_prescreener, int16_predictor, exp, alpha, beta, gamma, nrad, vcheck,
                                 bobbed, ChromaEdi, TFF, opencl, device)

    # InputType=2,3: use motion mask to blend luma between original clip & reweaved clip based on ProgSADMask setting. Use chroma from original clip in any case
    if InputType < 2:
        edi = edi1
    elif ProgSADMask <= 0:
        if not isGray:
            edi = core.std.ShufflePlanes([edi1, innerClip], planes=[0, 1, 2], colorfamily=Input.format.color_family)
        else:
            edi = edi1
    else:
        inputTypeBlend = core.mv.Mask(srchClip, bVec1, kind=1, ml=ProgSADMask)
        edi = core.std.MaskedMerge(innerClip, edi1, inputTypeBlend, planes=[0])

    # Get the max/min value for each pixel over neighboring motion-compensated frames - used for temporal sharpness limiting
    if TR1 > 0 or temporalSL:
        ediSuper = edi.mv.Super(pel=SubPel, sharp=SubPelInterp, levels=1, hpad=hpad, vpad=vpad)
    if temporalSL:
        bComp1 = core.mv.Compensate(edi, ediSuper, bVec1, thscd1=ThSCD1, thscd2=ThSCD2)
        fComp1 = core.mv.Compensate(edi, ediSuper, fVec1, thscd1=ThSCD1, thscd2=ThSCD2)
        tMax = core.std.Expr([core.std.Expr([edi, fComp1], expr=['x y max']), bComp1], expr=['x y max'])
        tMin = core.std.Expr([core.std.Expr([edi, fComp1], expr=['x y min']), bComp1], expr=['x y min'])
        if SLRad > 1:
            bComp3 = core.mv.Compensate(edi, ediSuper, bVec3, thscd1=ThSCD1, thscd2=ThSCD2)
            fComp3 = core.mv.Compensate(edi, ediSuper, fVec3, thscd1=ThSCD1, thscd2=ThSCD2)
            tMax = core.std.Expr([core.std.Expr([tMax, fComp3], expr=['x y max']), bComp3], expr=['x y max'])
            tMin = core.std.Expr([core.std.Expr([tMin, fComp3], expr=['x y min']), bComp3], expr=['x y min'])

    #---------------------------------------
    # Create basic output

    # Use motion vectors to blur interpolated image (edi) with motion-compensated previous and next frames. As above, this is done to remove shimmer from
    # alternate frames so the same binomial kernels are used. However, by using motion-compensated smoothing this time we avoid motion blur. The use of
    # MDegrain1 (motion compensated) rather than TemporalSmooth makes the weightings *look* different, but they evaluate to the same values
    # Create linear weightings of neighbors first                                                                      -2    -1    0     1     2
    if TR1 > 0: degrain1 = core.mv.Degrain1(edi, ediSuper, bVec1, fVec1, thsad=ThSAD1, thscd1=ThSCD1, thscd2=ThSCD2) # 0.00  0.33  0.33  0.33  0.00
    if TR1 > 1: degrain2 = core.mv.Degrain1(edi, ediSuper, bVec2, fVec2, thsad=ThSAD1, thscd1=ThSCD1, thscd2=ThSCD2) # 0.33  0.00  0.33  0.00  0.33

    # Combine linear weightings to give binomial weightings - TR1=0: (1), TR1=1: (1:2:1), TR1=2: (1:4:6:4:1)
    if TR1 <= 0:
        binomial1 = edi
    elif TR1 == 1:
        binomial1 = core.std.Merge(degrain1, edi, weight=[0.25])
    else:
        binomial1 = core.std.Merge(core.std.Merge(degrain1, degrain2, weight=[0.2]), edi, weight=[0.0625])

    # Remove areas of difference between smoothed image and interpolated image that are not bob-shimmer fixes: repairs residual motion blur from temporal smooth
    if Rep1 <= 0:
        repair1 = binomial1
    else:
        repair1 = QTGMC_KeepOnlyBobShimmerFixes(binomial1, edi, Rep1, RepChroma)

    # Apply source match - use difference between output and source to succesively refine output [extracted to function to clarify main code path]
    if SourceMatch <= 0:
        match = repair1
    else:
        match = QTGMC_ApplySourceMatch(repair1, InputType, ediInput, bVec1 if maxTR > 0 else None, fVec1 if maxTR > 0 else None, bVec2 if maxTR > 1 else None, fVec2 if maxTR > 1 else None, SubPel,
                                       SubPelInterp, hpad, vpad, ThSAD1, ThSCD1, ThSCD2, SourceMatch, MatchTR1, MatchEdi, MatchNNSize, MatchNNeurons, MatchEdiQual, MatchEdiMaxD, MatchTR2, MatchEdi2,
                                       MatchNNSize2, MatchNNeurons2, MatchEdiQual2, MatchEdiMaxD2, MatchEnhance, pscrn, int16_prescreener, int16_predictor, exp, alpha, beta, gamma, nrad, vcheck,
                                       TFF, opencl, device)

    # Lossless=2 - after preparing an interpolated, de-shimmered clip, restore the original source fields into it and clean up any artefacts
    # This mode will not give a true lossless result because the resharpening and final temporal smooth are still to come, but it will add further detail
    # However, it can introduce minor combing. This setting is best used together with source-match (it's effectively the final source-match stage)
    if Lossless >= 2:
        lossed1 = QTGMC_MakeLossless(match, innerClip, InputType, TFF)
    else:
        lossed1 = match

    #---------------------------------------
    # Resharpen / retouch output

    # Resharpen to counteract temporal blurs. Little sharpening needed for source-match mode since it has already recovered sharpness from source
    if SMode <= 0:
        resharp = lossed1
    elif SMode == 1:
        resharp = core.std.Expr([lossed1, lossed1.std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])], expr=[f'x x y - {sharpAdj} * +'])
    else:
        vresharp1 = core.std.Merge(lossed1.std.Maximum(coordinates=[0, 1, 0, 0, 0, 0, 1, 0]), lossed1.std.Minimum(coordinates=[0, 1, 0, 0, 0, 0, 1, 0]))
        if Precise:
            vresharp = core.std.Expr([vresharp1, lossed1], expr=['x y < x {i} + x y > x {i} - x ? ?'.format(i=scale(1, peak))]) # Precise mode: reduce tiny overshoot
        else:
            vresharp = vresharp1
        resharp = core.std.Expr([lossed1, vresharp.std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])], expr=[f'x x y - {sharpAdj} * +'])

    # Slightly thin down 1-pixel high horizontal edges that have been widened into neigboring field lines by the interpolator
    SVThinSc = SVThin * 6.0
    if SVThin > 0:
        expr = f'y x - {SVThinSc} * {neutral} +'
        vertMedD = core.std.Expr([lossed1, lossed1.rgvs.VerticalCleaner(mode=[1] if isGray else [1, 0])], expr=[expr] if isGray else [expr, ''])
        vertMedD = vertMedD.std.Convolution(matrix=[1, 2, 1], planes=[0], mode='h')
        expr = f'y {neutral} - abs x {neutral} - abs > y {neutral} ?'
        neighborD = core.std.Expr([vertMedD, vertMedD.std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1], planes=[0])], expr=[expr] if isGray else [expr, ''])
        thin = core.std.MergeDiff(resharp, neighborD, planes=[0])
    else:
        thin = resharp

    # Back blend the blurred difference between sharpened & unsharpened clip, before (1st) sharpness limiting (Sbb == 1,3). A small fidelity improvement
    if Sbb not in [1, 3]:
        backBlend1 = thin
    else:
        backBlend1 = core.std.MakeDiff(thin,
                                       Resize(core.std.MakeDiff(thin, lossed1, planes=[0]).std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1], planes=[0]),
                                              w, h, sw=w + epsilon, sh=h + epsilon, kernel='gauss', a1=5, dmode=1),
                                       planes=[0])

    # Limit over-sharpening by clamping to neighboring (spatial or temporal) min/max values in original
    # Occurs here (before final temporal smooth) if SLMode == 1,2. This location will restrict sharpness more, but any artefacts introduced will be smoothed
    if SLMode == 1:
        if SLRad <= 1:
            sharpLimit1 = core.rgvs.Repair(backBlend1, edi, mode=[1])
        else:
            sharpLimit1 = core.rgvs.Repair(backBlend1, core.rgvs.Repair(backBlend1, edi, mode=[12]), mode=[1])
    elif SLMode == 2:
        sharpLimit1 = Clamp(backBlend1, tMax, tMin, SOvs, SOvs)
    else:
        sharpLimit1 = backBlend1

    # Back blend the blurred difference between sharpened & unsharpened clip, after (1st) sharpness limiting (Sbb == 2,3). A small fidelity improvement
    if Sbb < 2:
        backBlend2 = sharpLimit1
    else:
        backBlend2 = core.std.MakeDiff(sharpLimit1,
                                       Resize(core.std.MakeDiff(sharpLimit1, lossed1, planes=[0]).std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1], planes=[0]),
                                              w, h, sw=w + epsilon, sh=h + epsilon, kernel='gauss', a1=5, dmode=1),
                                       planes=[0])

    # Add back any extracted noise, prior to final temporal smooth - this will restore detail that was removed as "noise" without restoring the noise itself
    # Average luma of FFT3DFilter extracted noise is 128.5, so deal with that too
    if GrainRestore <= 0:
        addNoise1 = backBlend2
    else:
        expr = f'x {noiseCentre} - {GrainRestore} * {neutral} +'
        addNoise1 = core.std.MergeDiff(backBlend2, finalNoise.std.Expr(expr=[expr] if ChromaNoise or isGray else [expr, '']), planes=CNplanes)

    # Final light linear temporal smooth for denoising
    if TR2 > 0:
        stableSuper = addNoise1.mv.Super(pel=SubPel, sharp=SubPelInterp, levels=1, hpad=hpad, vpad=vpad)
    if TR2 <= 0:
        stable = addNoise1
    elif TR2 == 1:
        stable = core.mv.Degrain1(addNoise1, stableSuper, bVec1, fVec1, thsad=ThSAD2, thscd1=ThSCD1, thscd2=ThSCD2)
    elif TR2 == 2:
        stable = core.mv.Degrain2(addNoise1, stableSuper, bVec1, fVec1, bVec2, fVec2, thsad=ThSAD2, thscd1=ThSCD1, thscd2=ThSCD2)
    else:
        stable = core.mv.Degrain3(addNoise1, stableSuper, bVec1, fVec1, bVec2, fVec2, bVec3, fVec3, thsad=ThSAD2, thscd1=ThSCD1, thscd2=ThSCD2)

    # Remove areas of difference between final output & basic interpolated image that are not bob-shimmer fixes: repairs motion blur caused by temporal smooth
    if Rep2 <= 0:
        repair2 = stable
    else:
        repair2 = QTGMC_KeepOnlyBobShimmerFixes(stable, edi, Rep2, RepChroma)

    # Limit over-sharpening by clamping to neighboring (spatial or temporal) min/max values in original
    # Occurs here (after final temporal smooth) if SLMode == 3,4. Allows more sharpening here, but more prone to introducing minor artefacts
    if SLMode == 3:
        if SLRad <= 1:
            sharpLimit2 = core.rgvs.Repair(repair2, edi, mode=[1])
        else:
            sharpLimit2 = core.rgvs.Repair(repair2, core.rgvs.Repair(repair2, edi, mode=[12]), mode=[1])
    elif SLMode >= 4:
        sharpLimit2 = Clamp(repair2, tMax, tMin, SOvs, SOvs)
    else:
        sharpLimit2 = repair2

    # Lossless=1 - inject source fields into result and clean up inevitable artefacts. Provided NoiseRestore=0.0 or 1.0, this mode will make the script result
    # properly lossless, but this will retain source artefacts and cause some combing (where the smoothed deinterlace doesn't quite match the source)
    if Lossless == 1:
        lossed2 = QTGMC_MakeLossless(sharpLimit2, innerClip, InputType, TFF)
    else:
        lossed2 = sharpLimit2

    # Add back any extracted noise, after final temporal smooth. This will appear as noise/grain in the output
    # Average luma of FFT3DFilter extracted noise is 128.5, so deal with that too
    if NoiseRestore <= 0:
        addNoise2 = lossed2
    else:
        expr = f'x {noiseCentre} - {NoiseRestore} * {neutral} +'
        addNoise2 = core.std.MergeDiff(lossed2, finalNoise.std.Expr(expr=[expr] if ChromaNoise or isGray else [expr, '']), planes=CNplanes)

    #---------------------------------------
    # Post-Processing

    # Shutter motion blur - get level of blur depending on output framerate and blur already in source
    blurLevel = (ShutterAngleOut * FPSDivisor - ShutterAngleSrc) * 100 / 360
    if blurLevel < 0:
        raise vs.Error('QTGMC: Cannot reduce motion blur already in source: increase ShutterAngleOut or FPSDivisor')
    if blurLevel > 200:
        raise vs.Error('QTGMC: Exceeded maximum motion blur level: decrease ShutterAngleOut or FPSDivisor')

    # ShutterBlur mode 2,3 - get finer resolution motion vectors to reduce blur "bleeding" into static areas
    rBlockDivide = [1, 1, 2, 4][ShutterBlur]
    rBlockSize = BlockSize // rBlockDivide
    rOverlap = Overlap // rBlockDivide
    rBlockSize = max(rBlockSize, 4)
    rOverlap = max(rOverlap, 2)
    rBlockDivide = BlockSize // rBlockSize
    rLambda = Lambda // (rBlockDivide * rBlockDivide)
    if ShutterBlur > 1:
        recalculate_args = dict(thsad=ThSAD1, blksize=rBlockSize, overlap=rOverlap, search=Search, searchparam=SearchParam, truemotion=TrueMotion, _lambda=rLambda, pnew=PNew, dct=DCT,
                                chroma=ChromaMotion)
        sbBVec1 = core.mv.Recalculate(srchSuper, bVec1, **recalculate_args)
        sbFVec1 = core.mv.Recalculate(srchSuper, fVec1, **recalculate_args)
    elif ShutterBlur > 0:
        sbBVec1 = bVec1
        sbFVec1 = fVec1

    # Shutter motion blur - use MFlowBlur to blur along motion vectors
    if ShutterBlur > 0:
        sblurSuper = addNoise2.mv.Super(pel=SubPel, sharp=SubPelInterp, levels=1, hpad=hpad, vpad=vpad)
        sblur = core.mv.FlowBlur(addNoise2, sblurSuper, sbBVec1, sbFVec1, blur=blurLevel, thscd1=ThSCD1, thscd2=ThSCD2)

    # Shutter motion blur - use motion mask to reduce blurring in areas of low motion - also helps reduce blur "bleeding" into static areas, then select blur type
    if ShutterBlur <= 0:
        sblurred = addNoise2
    elif SBlurLimit <= 0:
        sblurred = sblur
    else:
        sbMotionMask = core.mv.Mask(srchClip, bVec1, kind=0, ml=SBlurLimit)
        sblurred = core.std.MaskedMerge(addNoise2, sblur, sbMotionMask)

    # Reduce frame rate
    if FPSDivisor > 1:
        decimated = sblurred.std.SelectEvery(cycle=FPSDivisor, offsets=[0])
    else:
        decimated = sblurred

    # Crop off temporary vertical padding
    if Border:
        cropped = decimated.std.Crop(top=4, bottom=4)
    else:
        cropped = decimated

    # Show output of choice + settings
    if ShowNoise <= 0:
        output = cropped
    else:
        expr = f'x {neutral} - {ShowNoise} * {neutral} +'
        output = finalNoise.std.Expr(expr=[expr] if ChromaNoise or isGray else [expr, repr(neutral)])
    output = output.std.SetFieldBased(value=0)
    if not ShowSettings:
        return output
    else:
        text = f"TR0={TR0} | TR1={TR1} | TR2={TR2} | Rep0={Rep0} | Rep1={Rep1} | Rep2={Rep2} | RepChroma={RepChroma} | EdiMode='{EdiMode}' | NNSize={NNSize} | NNeurons={NNeurons} | " + \
               f"EdiQual={EdiQual} | EdiMaxD={EdiMaxD} | ChromaEdi='{ChromaEdi}' | Sharpness={Sharpness} | SMode={SMode} | SLMode={SLMode} | SLRad={SLRad} | SOvs={SOvs} | SVThin={SVThin} | " + \
               f"Sbb={Sbb} | SrchClipPP={SrchClipPP} | SubPel={SubPel} | SubPelInterp={SubPelInterp} | BlockSize={BlockSize} | Overlap={Overlap} | Search={Search} | SearchParam={SearchParam} | " + \
               f"PelSearch={PelSearch} | ChromaMotion={ChromaMotion} | TrueMotion={TrueMotion} | Lambda={Lambda} | LSAD={LSAD} | PNew={PNew} | PLevel={PLevel} | GlobalMotion={GlobalMotion} | " + \
               f"DCT={DCT} | ThSAD1={ThSAD1} | ThSAD2={ThSAD2} | ThSCD1={ThSCD1} | ThSCD2={ThSCD2} | SourceMatch={SourceMatch} | MatchPreset='{MatchPreset}' | MatchEdi='{MatchEdi}' | " + \
               f"MatchPreset2='{MatchPreset2}' | MatchEdi2='{MatchEdi2}' | MatchTR2={MatchTR2} | MatchEnhance={MatchEnhance} | Lossless={Lossless} | NoiseProcess={NoiseProcess} | " + \
               f"Denoiser='{Denoiser}' | FftThreads={FftThreads} | DenoiseMC={DenoiseMC} | NoiseTR={NoiseTR} | Sigma={Sigma} | ChromaNoise={ChromaNoise} | ShowNoise={ShowNoise} | " + \
               f"GrainRestore={GrainRestore} | NoiseRestore={NoiseRestore} | NoiseDeint='{NoiseDeint}' | StabilizeNoise={StabilizeNoise} | InputType={InputType} | ProgSADMask={ProgSADMask} | " + \
               f"FPSDivisor={FPSDivisor} | ShutterBlur={ShutterBlur} | ShutterAngleSrc={ShutterAngleSrc} | ShutterAngleOut={ShutterAngleOut} | SBlurLimit={SBlurLimit} | Border={Border} | " + \
               f"Precise={Precise} | Preset='{Preset}' | Tuning='{Tuning}' | ForceTR={ForceTR}"
        return output.text.Text(text=text)

#---------------------------------------
# Helpers

# Interpolate input clip using method given in EdiMode. Use Fallback or Bob as result if mode not in list. If ChromaEdi string if set then interpolate chroma
# separately with that method (only really useful for EEDIx). The function is used as main algorithm starting point and for first two source-match stages
def QTGMC_Interpolate(Input, InputType, EdiMode, NNSize, NNeurons, EdiQual, EdiMaxD, pscrn, int16_prescreener, int16_predictor, exp, alpha, beta, gamma, nrad, vcheck,
                      Fallback=None, ChromaEdi='', TFF=None, opencl=False, device=None):
    if opencl:
        nnedi3 = partial(core.nnedi3cl.NNEDI3CL, nsize=NNSize, nns=NNeurons, qual=EdiQual, pscrn=pscrn, device=device)
        eedi3 = partial(core.eedi3m.EEDI3CL, alpha=alpha, beta=beta, gamma=gamma, nrad=nrad, mdis=EdiMaxD, vcheck=vcheck, device=device)
    else:
        nnedi3 = partial(core.znedi3.nnedi3 if hasattr(core, 'znedi3') else core.nnedi3.nnedi3,
                         nsize=NNSize, nns=NNeurons, qual=EdiQual, pscrn=pscrn, int16_prescreener=int16_prescreener, int16_predictor=int16_predictor, exp=exp)
        eedi3 = partial(core.eedi3m.EEDI3 if hasattr(core, 'eedi3m') else core.eedi3.eedi3,
                        alpha=alpha, beta=beta, gamma=gamma, nrad=nrad, mdis=EdiMaxD, vcheck=vcheck)

    isGray = (Input.format.color_family == vs.GRAY)
    if isGray:
        ChromaEdi = ''
    planes = [0, 1, 2] if ChromaEdi == '' and not isGray else [0]
    field = 3 if TFF else 2

    if InputType == 1:
        return Input
    elif EdiMode == 'nnedi3':
        interp = nnedi3(Input, field=field, planes=planes)
    elif EdiMode == 'eedi3+nnedi3':
        interp = eedi3(Input, field=field, planes=planes, sclip=nnedi3(Input, field=field, planes=planes))
    elif EdiMode == 'eedi3':
        interp = eedi3(Input, field=field, planes=planes)
    else:
        if isinstance(Fallback, vs.VideoNode):
            interp = Fallback
        else:
            interp = Bob(Input, 0, 0.5, TFF)

    if ChromaEdi == 'nnedi3':
        interpuv = nnedi3(Input, field=field, planes=[1, 2], nsize=4, nns=0, qual=1)
    elif ChromaEdi == 'bob':
        interpuv = Bob(Input, 0, 0.5, TFF)
    else:
        return interp

    return core.std.ShufflePlanes([interp, interpuv], planes=[0, 1, 2], colorfamily=Input.format.color_family)

# Helper function: Compare processed clip with reference clip: only allow thin, horizontal areas of difference, i.e. bob shimmer fixes
# Rough algorithm: Get difference, deflate vertically by a couple of pixels or so, then inflate again. Thin regions will be removed
#                  by this process. Restore remaining areas of difference back to as they were in reference clip
def QTGMC_KeepOnlyBobShimmerFixes(Input, Ref, Rep=1, Chroma=True):
    isGray = (Input.format.color_family == vs.GRAY)
    planes = [0, 1, 2] if Chroma and not isGray else [0]

    neutral = 1 << (Input.format.bits_per_sample - 1)
    peak = (1 << Input.format.bits_per_sample) - 1

    # ed is the erosion distance - how much to deflate then reflate to remove thin areas of interest: 0 = minimum to 6 = maximum
    # od is over-dilation level  - extra inflation to ensure areas to restore back are fully caught:  0 = none to 3 = one full pixel
    # If Rep < 10, then ed = Rep and od = 0, otherwise ed = 10s digit and od = 1s digit (nasty method, but kept for compatibility with original TGMC)
    ed = Rep if Rep < 10 else Rep // 10
    od = 0 if Rep < 10 else Rep % 10

    diff = core.std.MakeDiff(Ref, Input)

    # Areas of positive difference                                                              # ed = 0 1 2 3 4 5 6 7
    choke1 = diff.std.Minimum(planes=planes, coordinates=[0, 1, 0, 0, 0, 0, 1, 0])              #      x x x x x x x x    1 pixel   \
    if ed > 2: choke1 = choke1.std.Minimum(planes=planes, coordinates=[0, 1, 0, 0, 0, 0, 1, 0]) #      . . . x x x x x    1 pixel    |  Deflate to remove thin areas
    if ed > 5: choke1 = choke1.std.Minimum(planes=planes, coordinates=[0, 1, 0, 0, 0, 0, 1, 0]) #      . . . . . . x x    1 pixel   /
    if ed % 3 != 0: choke1 = choke1.std.Deflate(planes=planes)                                  #      . x x . x x . x    A bit more deflate & some horizonal effect
    if ed in [2, 5]: choke1 = choke1.std.Median(planes=planes)                                  #      . . x . . x . .    Local median
    choke1 = choke1.std.Maximum(planes=planes, coordinates=[0, 1, 0, 0, 0, 0, 1, 0])            #      x x x x x x x x    1 pixel  \
    if ed > 1: choke1 = choke1.std.Maximum(planes=planes, coordinates=[0, 1, 0, 0, 0, 0, 1, 0]) #      . . x x x x x x    1 pixel   | Reflate again
    if ed > 4: choke1 = choke1.std.Maximum(planes=planes, coordinates=[0, 1, 0, 0, 0, 0, 1, 0]) #      . . . . . x x x    1 pixel  /

    # Over-dilation - extra reflation up to about 1 pixel
    if od == 1:
        choke1 = choke1.std.Inflate(planes=planes)
    elif od == 2:
        choke1 = choke1.std.Inflate(planes=planes).std.Inflate(planes=planes)
    elif od >= 3:
        choke1 = choke1.std.Maximum(planes=planes)

    # Areas of negative difference (similar to above)
    choke2 = diff.std.Maximum(planes=planes, coordinates=[0, 1, 0, 0, 0, 0, 1, 0])
    if ed > 2: choke2 = choke2.std.Maximum(planes=planes, coordinates=[0, 1, 0, 0, 0, 0, 1, 0])
    if ed > 5: choke2 = choke2.std.Maximum(planes=planes, coordinates=[0, 1, 0, 0, 0, 0, 1, 0])
    if ed % 3 != 0: choke2 = choke2.std.Inflate(planes=planes)
    if ed in [2, 5]: choke2 = choke2.std.Median(planes=planes)
    choke2 = choke2.std.Minimum(planes=planes, coordinates=[0, 1, 0, 0, 0, 0, 1, 0])
    if ed > 1: choke2 = choke2.std.Minimum(planes=planes, coordinates=[0, 1, 0, 0, 0, 0, 1, 0])
    if ed > 4: choke2 = choke2.std.Minimum(planes=planes, coordinates=[0, 1, 0, 0, 0, 0, 1, 0])

    if od == 1:
        choke2 = choke2.std.Deflate(planes=planes)
    elif od == 2:
        choke2 = choke2.std.Deflate(planes=planes).std.Deflate(planes=planes)
    elif od >= 3:
        choke2 = choke2.std.Minimum(planes=planes)

    # Combine above areas to find those areas of difference to restore
    expr1 = f'x {scale(129, peak)} < x y {neutral} < {neutral} y ? ?'
    expr2 = f'x {scale(127, peak)} > x y {neutral} > {neutral} y ? ?'
    restore = core.std.Expr([core.std.Expr([diff, choke1], expr=[expr1] if Chroma or isGray else [expr1, '']), choke2], expr=[expr2] if Chroma or isGray else [expr2, ''])

    return core.std.MergeDiff(Input, restore, planes=planes)

# Given noise extracted from an interlaced source (i.e. the noise is interlaced), generate "progressive" noise with a new "field" of noise injected. The new
# noise is centered on a weighted local average and uses the difference between local min & max as an estimate of local variance
def QTGMC_Generate2ndFieldNoise(Input, InterleavedClip, ChromaNoise=False, TFF=None):
    isGray = (Input.format.color_family == vs.GRAY)
    planes = [0, 1, 2] if ChromaNoise and not isGray else [0]

    neutral = 1 << (Input.format.bits_per_sample - 1)
    peak = (1 << Input.format.bits_per_sample) - 1

    origNoise = Input.std.SeparateFields(tff=TFF)
    noiseMax = origNoise.std.Maximum(planes=planes).std.Maximum(planes=planes, coordinates=[0, 0, 0, 1, 1, 0, 0, 0])
    noiseMin = origNoise.std.Minimum(planes=planes).std.Minimum(planes=planes, coordinates=[0, 0, 0, 1, 1, 0, 0, 0])
    random = InterleavedClip.std.SeparateFields(tff=TFF).std.BlankClip(color=[neutral] * Input.format.num_planes).grain.Add(var=1800, uvar=1800 if ChromaNoise else 0)
    expr = f'x {neutral} - y * {scale(256, peak)} / {neutral} +'
    varRandom = core.std.Expr([core.std.MakeDiff(noiseMax, noiseMin, planes=planes), random], expr=[expr] if ChromaNoise or isGray else [expr, ''])
    newNoise = core.std.MergeDiff(noiseMin, varRandom, planes=planes)

    return Weave(core.std.Interleave([origNoise, newNoise]), tff=TFF)

# Insert the source lines into the result to create a true lossless output. However, the other lines in the result have had considerable processing and won't
# exactly match source lines. There will be some slight residual combing. Use vertical medians to clean a little of this away
def QTGMC_MakeLossless(Input, Source, InputType, TFF):
    if InputType == 1:
        raise vs.Error('QTGMC: Lossless modes are incompatible with InputType=1')

    neutral = 1 << (Input.format.bits_per_sample - 1)

    # Weave the source fields and the "new" fields that have generated in the input
    if InputType <= 0:
        srcFields = Source.std.SeparateFields(tff=TFF)
    else:
        srcFields = Source.std.SeparateFields(tff=TFF).std.SelectEvery(cycle=4, offsets=[0, 3])
    newFields = Input.std.SeparateFields(tff=TFF).std.SelectEvery(cycle=4, offsets=[1, 2])
    processed = Weave(core.std.Interleave([srcFields, newFields]).std.SelectEvery(cycle=4, offsets=[0, 1, 3, 2]), tff=TFF)

    # Clean some of the artefacts caused by the above - creating a second version of the "new" fields
    vertMedian = processed.rgvs.VerticalCleaner(mode=[1])
    vertMedDiff = core.std.MakeDiff(processed, vertMedian)
    vmNewDiff1 = vertMedDiff.std.SeparateFields(tff=TFF).std.SelectEvery(cycle=4, offsets=[1, 2])
    expr = f'x {neutral} - y {neutral} - * 0 < {neutral} x {neutral} - abs y {neutral} - abs < x y ? ?'
    vmNewDiff2 = core.std.Expr([vmNewDiff1.rgvs.VerticalCleaner(mode=[1]), vmNewDiff1], expr=[expr])
    vmNewDiff3 = core.rgvs.Repair(vmNewDiff2, vmNewDiff2.rgvs.RemoveGrain(mode=[2]), mode=[1])

    # Reweave final result
    return Weave(core.std.Interleave([srcFields, core.std.MakeDiff(newFields, vmNewDiff3)]).std.SelectEvery(cycle=4, offsets=[0, 1, 3, 2]), tff=TFF)

# Source-match, a three stage process that takes the difference between deinterlaced input and the original interlaced source, to shift the input more towards
# the source without introducing shimmer. All other arguments defined in main script
def QTGMC_ApplySourceMatch(Deinterlace, InputType, Source, bVec1, fVec1, bVec2, fVec2, SubPel, SubPelInterp, hpad, vpad, ThSAD1, ThSCD1, ThSCD2, SourceMatch,
                           MatchTR1, MatchEdi, MatchNNSize, MatchNNeurons, MatchEdiQual, MatchEdiMaxD, MatchTR2, MatchEdi2, MatchNNSize2, MatchNNeurons2, MatchEdiQual2, MatchEdiMaxD2, MatchEnhance,
                           pscrn, int16_prescreener, int16_predictor, exp, alpha, beta, gamma, nrad, vcheck, TFF, opencl, device):
    # Basic source-match. Find difference between source clip & equivalent fields in interpolated/smoothed clip (called the "error" in formula below). Ideally
    # there should be no difference, we want the fields in the output to be as close as possible to the source whilst remaining shimmer-free. So adjust the
    # *source* in such a way that smoothing it will give a result closer to the unadjusted source. Then rerun the interpolation (edi) and binomial smooth with
    # this new source. Result will still be shimmer-free and closer to the original source.
    # Formula used for correction is P0' = P0 + (P0-P1)/(k+S(1-k)), where P0 is original image, P1 is the 1st attempt at interpolation/smoothing , P0' is the
    # revised image to use as new source for interpolation/smoothing, k is the weighting given to the current frame in the smooth, and S is a factor indicating
    # "temporal similarity" of the error from frame to frame, i.e. S = average over all pixels of [neighbor frame error / current frame error] . Decreasing
    # S will make the result sharper, sensible range is about -0.25 to 1.0. Empirically, S=0.5 is effective [will do deeper analysis later]
    errorTemporalSimilarity = 0.5 # S in formula described above
    errorAdjust1 = [1.0, 2.0 / (1.0 + errorTemporalSimilarity), 8.0 / (3.0 + 5.0 * errorTemporalSimilarity)][MatchTR1]
    if SourceMatch < 1 or InputType == 1:
        match1Clip = Deinterlace
    else:
        match1Clip = Weave(Deinterlace.std.SeparateFields(tff=TFF).std.SelectEvery(cycle=4, offsets=[0, 3]), tff=TFF)
    if SourceMatch < 1 or MatchTR1 <= 0:
        match1Update = Source
    else:
        match1Update = core.std.Expr([Source, match1Clip], expr=[f'x {errorAdjust1 + 1} * y {errorAdjust1} * -'])
    if SourceMatch > 0:
        match1Edi = QTGMC_Interpolate(match1Update, InputType, MatchEdi, MatchNNSize, MatchNNeurons, MatchEdiQual, MatchEdiMaxD, pscrn, int16_prescreener, int16_predictor, exp, alpha, beta, gamma,
                                      nrad, vcheck, TFF=TFF, opencl=opencl, device=device)
        if MatchTR1 > 0:
            match1Super = match1Edi.mv.Super(pel=SubPel, sharp=SubPelInterp, levels=1, hpad=hpad, vpad=vpad)
            match1Degrain1 = core.mv.Degrain1(match1Edi, match1Super, bVec1, fVec1, thsad=ThSAD1, thscd1=ThSCD1, thscd2=ThSCD2)
        if MatchTR1 > 1:
            match1Degrain2 = core.mv.Degrain1(match1Edi, match1Super, bVec2, fVec2, thsad=ThSAD1, thscd1=ThSCD1, thscd2=ThSCD2)
    if SourceMatch < 1:
        match1 = Deinterlace
    elif MatchTR1 <= 0:
        match1 = match1Edi
    elif MatchTR1 == 1:
        match1 = core.std.Merge(match1Degrain1, match1Edi, weight=[0.25])
    else:
        match1 = core.std.Merge(core.std.Merge(match1Degrain1, match1Degrain2, weight=[0.2]), match1Edi, weight=[0.0625])

    if SourceMatch < 2:
        return match1

    # Enhance effect of source-match stages 2 & 3 by sharpening clip prior to refinement (source-match tends to underestimate so this will leave result sharper)
    if SourceMatch > 1 and MatchEnhance > 0:
        match1Shp = core.std.Expr([match1, match1.std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])], expr=[f'x x y - {MatchEnhance} * +'])
    else:
        match1Shp = match1

    # Source-match refinement. Find difference between source clip & equivalent fields in (updated) interpolated/smoothed clip. Interpolate & binomially smooth
    # this difference then add it back to output. Helps restore differences that the basic match missed. However, as this pass works on a difference rather than
    # the source image it can be prone to occasional artefacts (difference images are not ideal for interpolation). In fact a lower quality interpolation such
    # as a simple bob often performs nearly as well as advanced, slower methods (e.g. NNEDI3)
    if SourceMatch < 2 or InputType == 1:
        match2Clip = match1Shp
    else:
        match2Clip = Weave(match1Shp.std.SeparateFields(tff=TFF).std.SelectEvery(cycle=4, offsets=[0, 3]), tff=TFF)
    if SourceMatch > 1:
        match2Diff = core.std.MakeDiff(Source, match2Clip)
        match2Edi = QTGMC_Interpolate(match2Diff, InputType, MatchEdi2, MatchNNSize2, MatchNNeurons2, MatchEdiQual2, MatchEdiMaxD2, pscrn, int16_prescreener, int16_predictor, exp, alpha, beta, gamma,
                                      nrad, vcheck, TFF=TFF, opencl=opencl, device=device)
        if MatchTR2 > 0:
            match2Super = match2Edi.mv.Super(pel=SubPel, sharp=SubPelInterp, levels=1, hpad=hpad, vpad=vpad)
            match2Degrain1 = core.mv.Degrain1(match2Edi, match2Super, bVec1, fVec1, thsad=ThSAD1, thscd1=ThSCD1, thscd2=ThSCD2)
        if MatchTR2 > 1:
            match2Degrain2 = core.mv.Degrain1(match2Edi, match2Super, bVec2, fVec2, thsad=ThSAD1, thscd1=ThSCD1, thscd2=ThSCD2)
    if SourceMatch < 2:
        match2 = match1
    elif MatchTR2 <= 0:
        match2 = match2Edi
    elif MatchTR2 == 1:
        match2 = core.std.Merge(match2Degrain1, match2Edi, weight=[0.25])
    else:
        match2 = core.std.Merge(core.std.Merge(match2Degrain1, match2Degrain2, weight=[0.2]), match2Edi, weight=[0.0625])

    # Source-match second refinement - correct error introduced in the refined difference by temporal smoothing. Similar to error correction from basic step
    errorAdjust2 = [1.0, 2.0 / (1.0 + errorTemporalSimilarity), 8.0 / (3.0 + 5.0 * errorTemporalSimilarity)][MatchTR2]
    if SourceMatch < 3 or MatchTR2 <= 0:
        match3Update = match2Edi
    else:
        match3Update = core.std.Expr([match2Edi, match2], expr=[f'x {errorAdjust2 + 1} * y {errorAdjust2} * -'])
    if SourceMatch > 2:
        if MatchTR2 > 0:
            match3Super = match3Update.mv.Super(pel=SubPel, sharp=SubPelInterp, levels=1, hpad=hpad, vpad=vpad)
            match3Degrain1 = core.mv.Degrain1(match3Update, match3Super, bVec1, fVec1, thsad=ThSAD1, thscd1=ThSCD1, thscd2=ThSCD2)
        if MatchTR2 > 1:
            match3Degrain2 = core.mv.Degrain1(match3Update, match3Super, bVec2, fVec2, thsad=ThSAD1, thscd1=ThSCD1, thscd2=ThSCD2)
    if SourceMatch < 3:
        match3 = match2
    elif MatchTR2 <= 0:
        match3 = match3Update
    elif MatchTR2 == 1:
        match3 = core.std.Merge(match3Degrain1, match3Update, weight=[0.25])
    else:
        match3 = core.std.Merge(core.std.Merge(match3Degrain1, match3Degrain2, weight=[0.2]), match3Update, weight=[0.0625])

    # Apply difference calculated in source-match refinement
    return core.std.MergeDiff(match1Shp, match3)


# based on smartfade v0.2 by martino - Aimed at removing interlaced fades in anime. Uses luma difference between two fields as activation threshold.
# mod by thetoof : removed degrainmedian post-processing
#                  changed how the fields are blended together (average of 2 nnedi interpolations + contra-sharpening to retain more detail)
#
# Parameters:
#  threshold (float) - Threshold for fade detection. Default is 0.4
#  show (bool)       - Display luma difference between fields without processing anything. Default is false
def smartfademod(clip, threshold=0.4, show=False, tff=None):
    def frame_eval(n, f, orig, defade):
        diff = abs(f[0].props['PlaneStatsAverage'] - f[1].props['PlaneStatsAverage']) * 255
        if show:
            return orig.text.Text(text=diff)
        return defade if diff > threshold else orig

    if not isinstance(clip, vs.VideoNode):
        raise vs.Error('smartfademod: This is not a clip')

    if not isinstance(tff, bool):
        raise vs.Error("smartfademod: 'tff' must be set. Setting tff to true means top field first and false means bottom field first")

    sep = clip.std.SeparateFields(tff=tff)
    even = sep[::2].std.PlaneStats()
    odd = sep[1::2].std.PlaneStats()
    return clip.std.FrameEval(eval=partial(frame_eval, orig=clip, defade=daa(clip)), prop_src=[even, odd])


###### srestore v2.7e ######
def srestore(source, frate=None, omode=6, speed=None, mode=2, thresh=16, dclip=None):
    if not isinstance(source, vs.VideoNode):
        raise vs.Error('srestore: This is not a clip')

    if source.format.color_family != vs.YUV:
        raise vs.Error('srestore: Only YUV format is supported')

    if dclip is None:
        dclip = source
    elif not isinstance(dclip, vs.VideoNode):
        raise vs.Error("srestore: 'dclip' is not a clip")
    elif dclip.format.color_family != vs.YUV:
        raise vs.Error('srestore: Only YUV format is supported')

    neutral = 1 << (source.format.bits_per_sample - 1)
    peak = (1 << source.format.bits_per_sample) - 1

    ###### parameters & other necessary vars ######
    srad = math.sqrt(abs(speed)) * 4 if speed is not None and abs(speed) >= 1 else 12
    irate = source.fps_num / source.fps_den
    bsize = 16 if speed is not None and speed > 0 else 32
    bom = isinstance(omode, str)
    thr = abs(thresh) + 0.01

    if bom or abs(omode - 3) < 2.5:
        frfac = 1
    elif frate is not None:
        if frate * 5 < irate or frate > irate:
            frfac = 1
        else:
            frfac = abs(frate) / irate
    elif cround(irate * 10010) % 30000 == 0:
        frfac = 1001 / 2400
    else:
        frfac = 480 / 1001

    if abs(frfac * 1001 - cround(frfac * 1001)) < 0.01:
        numr = cround(frfac * 1001)
    elif abs(1001 / frfac - cround(1001 / frfac)) < 0.01:
        numr = 1001
    else:
        numr = cround(frfac * 9000)
    if frate is not None and abs(irate * numr / cround(numr / frfac) - frate) > abs(irate * cround(frate * 100) / cround(irate * 100) - frate):
        numr = cround(frate * 100)
    denm = cround(numr / frfac)

    ###### source preparation & lut ######
    if abs(mode) >= 2 and not bom:
        mec = core.std.Merge(core.std.Merge(source, source.std.Trim(first=1), weight=[0, 0.5]), source.std.Trim(first=1), weight=[0.5, 0])

    if dclip.format.id != vs.YUV420P8:
        dclip = dclip.resize.Bicubic(format=vs.YUV420P8)
    dclip = dclip.resize.Point(dclip.width if srad == 4 else int(dclip.width / 2 / srad + 4) * 4, dclip.height if srad == 4 else int(dclip.height / 2 / srad + 4) * 4).std.Trim(first=2)
    if mode < 0:
        dclip = core.std.StackVertical([core.std.StackHorizontal([mvf.GetPlane(dclip, 1), mvf.GetPlane(dclip, 2)]), mvf.GetPlane(dclip, 0)])
    else:
        dclip = mvf.GetPlane(dclip, 0)
    if bom:
        dclip = dclip.std.Expr(expr=['x 0.5 * 64 +'])

    expr1 = 'x 128 - y 128 - * 0 > x 128 - abs y 128 - abs < x 128 - 128 x - * y 128 - 128 y - * ? x y + 256 - dup * ? 0.25 * 128 +'
    expr2 = 'x y - dup * 3 * x y + 256 - dup * - 128 +'
    diff = core.std.MakeDiff(dclip, dclip.std.Trim(first=1))
    if not bom:
        bclp = core.std.Expr([diff, diff.std.Trim(first=1)], expr=[expr1]).resize.Bilinear(bsize, bsize)
    else:
        bclp = core.std.Expr([diff.std.Trim(first=1), core.std.MergeDiff(diff, diff.std.Trim(first=2))], expr=[expr2]).resize.Bilinear(bsize, bsize)
    dclp = diff.std.Trim(first=1).std.Lut(function=lambda x: max(cround(abs(x - 128) ** 1.1 - 1), 0)).resize.Bilinear(bsize, bsize)

    ###### postprocessing ######
    if bom:
        sourceDuplicate = source.std.DuplicateFrames(frames=[0])
        sourceTrim1 = source.std.Trim(first=1)
        sourceTrim2 = source.std.Trim(first=2)

        unblend1 = core.std.Expr([sourceDuplicate, source], expr=['x -1 * y 2 * +'])
        unblend2 = core.std.Expr([sourceTrim1, sourceTrim2], expr=['x 2 * y -1 * +'])

        qmask1 = core.std.MakeDiff(unblend1.std.Convolution(matrix=[1, 1, 1, 1, 0, 1, 1, 1, 1], planes=[0]), unblend1, planes=[0])
        qmask2 = core.std.MakeDiff(unblend2.std.Convolution(matrix=[1, 1, 1, 1, 0, 1, 1, 1, 1], planes=[0]), unblend2, planes=[0])
        diffm = core.std.MakeDiff(sourceDuplicate, source, planes=[0]).std.Maximum(planes=[0])
        bmask = core.std.Expr([qmask1, qmask2], expr=[f'x {neutral} - dup * dup y {neutral} - dup * + / {peak} *', ''])
        expr = 'x 2 * y < x {i} < and 0 y 2 * x < y {i} < and {peak} x x y + / {j} * {k} + ? ?'.format(i=scale(4, peak), peak=peak, j=scale(200, peak), k=scale(28, peak))
        dmask = core.std.Expr([diffm, diffm.std.Trim(first=2)], expr=[expr, ''])
        pmask = core.std.Expr([dmask, bmask], expr=[f'y 0 > y {peak} < and x 0 = x {peak} = or and x y ?', ''])

        matrix = [1, 2, 1, 2, 4, 2, 1, 2, 1]

        omode = omode.lower()
        if omode == 'pp0':
            fin = core.std.Expr([sourceDuplicate, source, sourceTrim1, sourceTrim2], expr=['x -0.5 * y + z + a -0.5 * +'])
        elif omode == 'pp1':
            fin = core.std.MaskedMerge(unblend1, unblend2, dmask.std.Convolution(matrix=matrix, planes=[0]).std.Expr(expr=['', repr(neutral)]))
        elif omode == 'pp2':
            fin = core.std.MaskedMerge(unblend1, unblend2, bmask.std.Convolution(matrix=matrix, planes=[0]), first_plane=True)
        elif omode == 'pp3':
            fin = core.std.MaskedMerge(unblend1, unblend2, pmask.std.Convolution(matrix=matrix, planes=[0]), first_plane=True).std.Convolution(matrix=matrix, planes=[1, 2])
        else:
            raise vs.Error('srestore: unexpected value for omode')

    ###### initialise variables ######
    lfr = -100
    offs = 0
    ldet = -100
    lpos = 0
    d32 = d21 = d10 = d01 = d12 = d23 = d34 = None
    m42 = m31 = m20 = m11 = m02 = m13 = m24 = None
    bp2 = bp1 = bn0 = bn1 = bn2 = bn3 = None
    cp2 = cp1 = cn0 = cn1 = cn2 = cn3 = None

    def srestore_inside(n, f):
        nonlocal lfr, offs, ldet, lpos, d32, d21, d10, d01, d12, d23, d34, m42, m31, m20, m11, m02, m13, m24, bp2, bp1, bn0, bn1, bn2, bn3, cp2, cp1, cn0, cn1, cn2, cn3

        ### preparation ###
        jmp = lfr + 1 == n
        cfo = ((n % denm) * numr * 2 + denm + numr) % (2 * denm) - denm
        bfo = cfo > -numr and cfo <= numr
        lfr = n
        offs = offs + 2 * denm if bfo and offs <= -4 * numr else offs - 2 * denm if bfo and offs >= 4 * numr else offs
        pos = 0 if frfac == 1 else -cround((cfo + offs) / (2 * numr)) if bfo else lpos
        cof = cfo + offs + 2 * numr * pos
        ldet = -1 if n + pos == ldet else n + pos

        ### diff value shifting ###
        d_v = f[1].props['PlaneStatsMax'] + 0.015625
        if jmp:
            d43 = d32
            d32 = d21
            d21 = d10
            d10 = d01
            d01 = d12
            d12 = d23
            d23 = d34
        else:
            d43 = d32 = d21 = d10 = d01 = d12 = d23 = d_v
        d34 = d_v

        m_v = f[2].props['PlaneStatsDiff'] * 255 + 0.015625 if not bom and abs(omode) > 5 else 1
        if jmp:
            m53 = m42
            m42 = m31
            m31 = m20
            m20 = m11
            m11 = m02
            m02 = m13
            m13 = m24
        else:
            m53 = m42 = m31 = m20 = m11 = m02 = m13 = m_v
        m24 = m_v

        ### get blend and clear values ###
        b_v = 128 - f[0].props['PlaneStatsMin']
        if b_v < 1:
            b_v = 0.125
        c_v = f[0].props['PlaneStatsMax'] - 128
        if c_v < 1:
            c_v = 0.125

        ### blend value shifting ###
        if jmp:
            bp3 = bp2
            bp2 = bp1
            bp1 = bn0
            bn0 = bn1
            bn1 = bn2
            bn2 = bn3
        else:
            bp3 = b_v - c_v if bom else b_v
            bp2 = bp1 = bn0 = bn1 = bn2 = bp3
        bn3 = b_v - c_v if bom else b_v

        ### clear value shifting ###
        if jmp:
            cp3 = cp2
            cp2 = cp1
            cp1 = cn0
            cn0 = cn1
            cn1 = cn2
            cn2 = cn3
        else:
            cp3 = cp2 = cp1 = cn0 = cn1 = cn2 = c_v
        cn3 = c_v

        ### used detection values ###
        bb = [bp3, bp2, bp1, bn0, bn1][pos + 2]
        bc = [bp2, bp1, bn0, bn1, bn2][pos + 2]
        bn = [bp1, bn0, bn1, bn2, bn3][pos + 2]

        cb = [cp3, cp2, cp1, cn0, cn1][pos + 2]
        cc = [cp2, cp1, cn0, cn1, cn2][pos + 2]
        cn = [cp1, cn0, cn1, cn2, cn3][pos + 2]

        dbb = [d43, d32, d21, d10, d01][pos + 2]
        dbc = [d32, d21, d10, d01, d12][pos + 2]
        dcn = [d21, d10, d01, d12, d23][pos + 2]
        dnn = [d10, d01, d12, d23, d34][pos + 2]
        dn2 = [d01, d12, d23, d34, d34][pos + 2]

        mb1 = [m53, m42, m31, m20, m11][pos + 2]
        mb = [m42, m31, m20, m11, m02][pos + 2]
        mc = [m31, m20, m11, m02, m13][pos + 2]
        mn = [m20, m11, m02, m13, m24][pos + 2]
        mn1 = [m11, m02, m13, m24, 0.01][pos + 2]

        ### basic calculation ###
        bbool = 0.8 * bc * cb > bb * cc and 0.8 * bc * cn > bn * cc and bc * bc > cc
        blend = bbool and bc * 5 > cc and dbc + dcn > 1.5 * thr and (dbb < 7 * dbc or dbb < 8 * dcn) and (dnn < 8 * dcn or dnn < 7 * dbc) and (mb < mb1 and mb < mc or mn < mn1 and mn < mc or (dbb + dnn) * 4 < dbc + dcn or (bb * cc * 5 < bc * cb or mb > thr) and (bn * cc * 5 < bc * cn or mn > thr) and bc > thr)
        clear = dbb + dbc > thr and dcn + dnn > thr and (bc < 2 * bb or bc < 2 * bn) and (dbb + dnn) * 2 > dbc + dcn and (mc < 0.96 * mb and mc < 0.96 * mn and (bb * 2 > cb or bn * 2 > cn) and cc > cb and cc > cn or frfac > 0.45 and frfac < 0.55 and 0.8 * mc > mb1 and 0.8 * mc > mn1 and mb > 0.8 * mn and mn > 0.8 * mb)
        highd = dcn > 5 * dbc and dcn > 5 * dnn and dcn > thr and dbc < thr and dnn < thr
        lowd = dcn * 5 < dbc and dcn * 5 < dnn and dbc > thr and dnn > thr and dcn < thr and frfac > 0.35 and (frfac < 0.51 or dcn * 5 < dbb)
        res = d43 < thr and d32 < thr and d21 < thr and d10 < thr and d01 < thr and d12 < thr and d23 < thr and d34 < thr or dbc * 4 < dbb and dcn * 4 < dbb and dnn * 4 < dbb and dn2 * 4 < dbb or dcn * 4 < dbc and dnn * 4 < dbc and dn2 * 4 < dbc

        ### offset calculation ###
        if blend:
            odm = denm
        elif clear:
            odm = 0
        elif highd:
            odm = denm - numr
        elif lowd:
            odm = 2 * denm - numr
        else:
            odm = cof
        odm += cround((cof - odm) / (2 * denm)) * 2 * denm

        if blend:
            odr = denm - numr
        elif clear or highd:
            odr = numr
        elif frfac < 0.5:
            odr = 2 * numr
        else:
            odr = 2 * (denm - numr)
        odr *= 0.9

        if ldet >= 0:
            if cof > odm + odr:
                if cof - offs - odm - odr > denm and res:
                    cof = odm + 2 * denm - odr
                else:
                    cof = odm + odr
            elif cof < odm - odr:
                if offs > denm and res:
                    cof = odm - 2 * denm + odr
                else:
                    cof = odm - odr
            elif offs < -1.15 * denm and res:
                cof += 2 * denm
            elif offs > 1.25 * denm and res:
                cof -= 2 * denm

        offs = 0 if frfac == 1 else cof - cfo - 2 * numr * pos
        lpos = pos
        opos = 0 if frfac == 1 else -cround((cfo + offs + (denm if bfo and offs <= -4 * numr else 0)) / (2 * numr))
        pos = min(max(opos, -2), 2)

        ### frame output calculation - resync - dup ###
        dbb = [d43, d32, d21, d10, d01][pos + 2]
        dbc = [d32, d21, d10, d01, d12][pos + 2]
        dcn = [d21, d10, d01, d12, d23][pos + 2]
        dnn = [d10, d01, d12, d23, d34][pos + 2]

        ### dup_hq - merge ###
        if opos != pos or abs(mode) < 2 or abs(mode) == 3:
            dup = 0
        elif dcn * 5 < dbc and dnn * 5 < dbc and (dcn < 1.25 * thr or bn < bc and pos == lpos) or (dcn * dcn < dbc or dcn * 5 < dbc) and bn < bc and pos == lpos and dnn < 0.9 * dbc or dnn * 9 < dbc and dcn * 3 < dbc:
            dup = 1
        elif (dbc * dbc < dcn or dbc * 5 < dcn) and bb < bc and pos == lpos and dbb < 0.9 * dcn or dbb * 9 < dcn and dbc * 3 < dcn or dbb * 5 < dcn and dbc * 5 < dcn and (dbc < 1.25 * thr or bb < bc and pos == lpos):
            dup = -1
        else:
            dup = 0
        mer = not bom and opos == pos and dup == 0 and abs(mode) > 2 and (dbc * 8 < dcn or dbc * 8 < dbb or dcn * 8 < dbc or dcn * 8 < dnn or dbc * 2 < thr or dcn * 2 < thr or dnn * 9 < dbc and dcn * 3 < dbc or dbb * 9 < dcn and dbc * 3 < dcn)

        ### deblend - doubleblend removal - postprocessing ###
        add = bp1 * cn2 > bn2 * cp1 * (1 + thr * 0.01) and bn0 * cn2 > bn2 * cn0 * (1 + thr * 0.01) and cn2 * bn1 > cn1 * bn2 * (1 + thr * 0.01)
        if bom:
            if bn0 > bp2 and bn0 >= bp1 and bn0 > bn1 and bn0 > bn2 and cn0 < 125:
                if d12 * d12 < d10 or d12 * 9 < d10:
                    dup = 1
                elif d10 * d10 < d12 or d10 * 9 < d12:
                    dup = 0
                else:
                    dup = 4
            elif bp1 > bp3 and bp1 >= bp2 and bp1 > bn0 and bp1 > bn1:
                dup = 1
            else:
                dup = 0
        elif dup == 0:
            if omode > 0 and omode < 5:
                if not bbool:
                    dup = 0
                elif omode == 4 and bp1 * cn1 < bn1 * cp1 or omode == 3 and d10 < d01 or omode == 1:
                    dup = -1
                else:
                    dup = 1
            elif omode == 5:
                if bp1 * cp2 > bp2 * cp1 * (1 + thr * 0.01) and bn0 * cp2 > bp2 * cn0 * (1 + thr * 0.01) and cp2 * bn1 > cn1 * bp2 * (1 + thr * 0.01) and (not add or cp2 * bn2 > cn2 * bp2):
                    dup = -2
                elif add:
                    dup = 2
                elif bn0 * cp1 > bp1 * cn0 and (bn0 * cn1 < bn1 * cn0 or cp1 * bn1 > cn1 * bp1):
                    dup = -1
                elif bn0 * cn1 > bn1 * cn0:
                    dup = 1
                else:
                    dup = 0
            else:
                dup = 0

        ### output clip ###
        if dup == 4:
            return fin
        else:
            oclp = mec if mer and dup == 0 else source
            opos += dup - (1 if dup == 0 and mer and dbc < dcn else 0)
            if opos < 0:
                return oclp.std.DuplicateFrames(frames=[0] * -opos)
            else:
                return oclp.std.Trim(first=opos)

    ###### evaluation call & output calculation ######
    bclpYStats = bclp.std.PlaneStats()
    dclpYStats = dclp.std.PlaneStats()
    dclipYStats = core.std.PlaneStats(dclip, dclip.std.Trim(first=2))
    last = source.std.FrameEval(eval=srestore_inside, prop_src=[bclpYStats, dclpYStats, dclipYStats])

    ###### final decimation ######
    return ChangeFPS(last.std.Cache(make_linear=True), source.fps_num * numr, source.fps_den * denm)


# frame_ref = start of AABCD pattern
def dec_txt60mc(src, frame_ref, srcbob=False, draft=False, tff=None, opencl=False, device=None):
    if not isinstance(src, vs.VideoNode):
        raise vs.Error('dec_txt60mc: This is not a clip')

    if not (srcbob or isinstance(tff, bool)):
        raise vs.Error("dec_txt60mc: 'tff' must be set when srcbob=False. Setting tff to true means top field first and false means bottom field first")

    field_ref = frame_ref if srcbob else frame_ref * 2
    field_ref %= 5
    invpos = (5 - field_ref) % 5
    pel = 1 if draft else 2
    blksize = 16 if src.width > 1024 or src.height > 576 else 8
    overlap = blksize // 2

    if srcbob:
        last = src
    elif draft:
        last = Bob(src, tff=tff)
    else:
        last = QTGMC(src, TR0=1, TR1=1, TR2=1, SourceMatch=3, Lossless=2, TFF=tff, opencl=opencl, device=device)

    clean = last.std.SelectEvery(cycle=5, offsets=[4 - invpos])
    if invpos > 2:
        jitter = core.std.AssumeFPS(last.std.Trim(length=1) * 2 + last.std.SelectEvery(cycle=5, offsets=[6 - invpos, 7 - invpos]), fpsnum=24000, fpsden=1001)
    elif invpos > 1:
        jitter = core.std.AssumeFPS(last.std.Trim(length=1) + last.std.SelectEvery(cycle=5, offsets=[2 - invpos, 6 - invpos]), fpsnum=24000, fpsden=1001)
    else:
        jitter = last.std.SelectEvery(cycle=5, offsets=[1 - invpos, 2 - invpos])
    jsup_pre = DitherLumaRebuild(jitter, s0=1).mv.Super(pel=pel)
    jsup = jitter.mv.Super(pel=pel, levels=1)
    vect_f = jsup_pre.mv.Analyse(blksize=blksize, isb=False, delta=1, overlap=overlap)
    vect_b = jsup_pre.mv.Analyse(blksize=blksize, isb=True, delta=1, overlap=overlap)
    comp = core.mv.FlowInter(jitter, jsup, vect_b, vect_f)
    fixed = comp[::2]
    last = core.std.Interleave([fixed, clean])
    return last.std.Trim(first=invpos // 3)


# 30pテロ部を24pに変換して返す
def ivtc_txt30mc(src, frame_ref, draft=False, tff=None, opencl=False, device=None):
    if not isinstance(src, vs.VideoNode):
        raise vs.Error('ivtc_txt30mc: This is not a clip')

    if not isinstance(tff, bool):
        raise vs.Error("ivtc_txt30mc: 'tff' must be set. Setting tff to true means top field first and false means bottom field first")

    frame_ref %= 5
    offset = [0, 0, -1, 1, 1][frame_ref]
    pattern = [0, 1, 0, 0, 1][frame_ref]
    direction = [-1, -1, 1, 1, 1][frame_ref]
    pel = 1 if draft else 2
    blksize = 16 if src.width > 1024 or src.height > 576 else 8
    overlap = blksize // 2

    if draft:
        last = Bob(src, tff=tff)
    else:
        last = QTGMC(src, TR0=1, TR1=1, TR2=1, SourceMatch=3, Lossless=2, TFF=tff, opencl=opencl, device=device)

    if pattern == 0:
        if offset == -1:
            c1 = core.std.AssumeFPS(last.std.Trim(length=1) + last.std.SelectEvery(cycle=10, offsets=[2 + offset, 7 + offset, 5 + offset, 10 + offset]), fpsnum=24000, fpsden=1001)
        else:
            c1 = last.std.SelectEvery(cycle=10, offsets=[offset, 2 + offset, 7 + offset, 5 + offset])
        if offset == 1:
            part1 = last.std.SelectEvery(cycle=10, offsets=[4])
            part2 = last.std.SelectEvery(cycle=10, offsets=[5])
            part3 = last.std.Trim(first=10).std.SelectEvery(cycle=10, offsets=[0])
            part4 = last.std.SelectEvery(cycle=10, offsets=[9])
            c2 = core.std.Interleave([part1, part2, part3, part4])
        else:
            c2 = last.std.SelectEvery(cycle=10, offsets=[3 + offset, 4 + offset, 9 + offset, 8 + offset])
    else:
        if offset == 1:
            part1 = last.std.SelectEvery(cycle=10, offsets=[3])
            part2 = last.std.SelectEvery(cycle=10, offsets=[5])
            part3 = last.std.Trim(first=10).std.SelectEvery(cycle=10, offsets=[0])
            part4 = last.std.SelectEvery(cycle=10, offsets=[8])
            c1 = core.std.Interleave([part1, part2, part3, part4])
        else:
            c1 = last.std.SelectEvery(cycle=10, offsets=[2 + offset, 4 + offset, 9 + offset, 7 + offset])
        if offset == -1:
            c2 = core.std.AssumeFPS(last.std.Trim(length=1) + last.std.SelectEvery(cycle=10, offsets=[1 + offset, 6 + offset, 5 + offset, 10 + offset]), fpsnum=24000, fpsden=1001)
        else:
            c2 = last.std.SelectEvery(cycle=10, offsets=[offset, 1 + offset, 6 + offset, 5 + offset])

    super1_pre = DitherLumaRebuild(c1, s0=1).mv.Super(pel=pel)
    super1 = c1.mv.Super(pel=pel, levels=1)
    vect_f1 = super1_pre.mv.Analyse(blksize=blksize, isb=False, delta=1, overlap=overlap)
    vect_b1 = super1_pre.mv.Analyse(blksize=blksize, isb=True, delta=1, overlap=overlap)
    fix1 = core.mv.FlowInter(c1, super1, vect_b1, vect_f1, time=50 + direction * 25).std.SelectEvery(cycle=4, offsets=[0, 2])

    super2_pre = DitherLumaRebuild(c2, s0=1).mv.Super(pel=pel)
    super2 = c2.mv.Super(pel=pel, levels=1)
    vect_f2 = super2_pre.mv.Analyse(blksize=blksize, isb=False, delta=1, overlap=overlap)
    vect_b2 = super2_pre.mv.Analyse(blksize=blksize, isb=True, delta=1, overlap=overlap)
    fix2 = core.mv.FlowInter(c2, super2, vect_b2, vect_f2).std.SelectEvery(cycle=4, offsets=[0, 2])

    if pattern == 0:
        return core.std.Interleave([fix1, fix2])
    else:
        return core.std.Interleave([fix2, fix1])


# Version 1.1
# frame_ref = start of clean-combed-combed-clean-clean pattern
def ivtc_txt60mc(src, frame_ref, srcbob=False, draft=False, tff=None, opencl=False, device=None):
    if not isinstance(src, vs.VideoNode):
        raise vs.Error('ivtc_txt60mc: This is not a clip')

    if not (srcbob or isinstance(tff, bool)):
        raise vs.Error("ivtc_txt60mc: 'tff' must be set when srcbob=False. Setting tff to true means top field first and false means bottom field first")

    field_ref = frame_ref if srcbob else frame_ref * 2
    field_ref %= 5
    invpos = (5 - field_ref) % 5
    pel = 1 if draft else 2
    blksize = 16 if src.width > 1024 or src.height > 576 else 8
    overlap = blksize // 2

    if srcbob:
        last = src
    elif draft:
        last = Bob(src, tff=tff)
    else:
        last = QTGMC(src, TR0=1, TR1=1, TR2=1, SourceMatch=3, Lossless=2, TFF=tff, opencl=opencl, device=device)

    if invpos > 1:
        clean = core.std.AssumeFPS(last.std.Trim(length=1) + last.std.SelectEvery(cycle=5, offsets=[6 - invpos]), fpsnum=12000, fpsden=1001)
    else:
        clean = last.std.SelectEvery(cycle=5, offsets=[1 - invpos])
    if invpos > 3:
        jitter = core.std.AssumeFPS(last.std.Trim(length=1) + last.std.SelectEvery(cycle=5, offsets=[4 - invpos, 8 - invpos]), fpsnum=24000, fpsden=1001)
    else:
        jitter = last.std.SelectEvery(cycle=5, offsets=[3 - invpos, 4 - invpos])
    jsup_pre = DitherLumaRebuild(jitter, s0=1).mv.Super(pel=pel)
    jsup = jitter.mv.Super(pel=pel, levels=1)
    vect_f = jsup_pre.mv.Analyse(blksize=blksize, isb=False, delta=1, overlap=overlap)
    vect_b = jsup_pre.mv.Analyse(blksize=blksize, isb=True, delta=1, overlap=overlap)
    comp = core.mv.FlowInter(jitter, jsup, vect_b, vect_f)
    fixed = comp[::2]
    last = core.std.Interleave([clean, fixed])
    return last.std.Trim(first=invpos // 2)


#################################################
###                                           ###
###                  logoNR                   ###
###                                           ###
###      by 06_taro - astrataro@gmail.com     ###
###                                           ###
###            v0.1 - 22 March 2012           ###
###                                           ###
#################################################
###
### Post-denoise filter of EraseLogo.
### Only process logo areas in logo frames, even if l/t/r/b are not set. Non-logo areas are left untouched.
###
###
### +---------+
### |  USAGE  |
### +---------+
###
### dlg [clip]
### ------------------
###    Clip after delogo.
###
### src [clip]
### ------------------
###    Clip before delogo.
###
### chroma [bool, default: True]
### ------------------
###    Process chroma plane or not.
###
### l/t/r/b [int, default: 0]
### ------------------
###    left/top/right/bottom pixels to be cropped for logo area.
###    Have the same restriction as Crop, e.g., no odd value for YV12.
###    logoNR only filters the logo areas in logo frames, no matter l/t/r/b are set or not.
###    So if you have other heavy filters running in a pipeline and don't care much about the speed of logoNR,
###    it is safe to left these values unset.
###    Setting these values only makes logoNR run faster, with rarely noticeable difference in result,
###    unless you set wrong values and the logo is not covered in your cropped target area.
###
### d/a/s/h [int, default: 1/2/2/3]
### ------------------
###    The same parameters of KNLMeansCL.
###
### +----------------+
### |  REQUIREMENTS  |
### +----------------+
###
### -> KNLMeansCL
### -> RGVS
def logoNR(dlg, src, chroma=True, l=0, t=0, r=0, b=0, d=1, a=2, s=2, h=3):
    if not (isinstance(dlg, vs.VideoNode) and isinstance(src, vs.VideoNode)):
        raise vs.Error('logoNR: This is not a clip')

    if dlg.format.id != src.format.id:
        raise vs.Error('logoNR: Clips must be the same format')

    if dlg.format.color_family == vs.GRAY:
        chroma = False

    if not chroma and dlg.format.color_family != vs.GRAY:
        dlg_orig = dlg
        dlg = mvf.GetPlane(dlg, 0)
        src = mvf.GetPlane(src, 0)
    else:
        dlg_orig = None

    b_crop = (l != 0) or (t != 0) or (r != 0) or (b != 0)
    if b_crop:
        src = src.std.Crop(left=l, right=r, top=t, bottom=b)
        last = dlg.std.Crop(left=l, right=r, top=t, bottom=b)
    else:
        last = dlg

    if chroma:
        clp_nr = KNLMeansCL(last, d=d, a=a, s=s, h=h)
    else:
        clp_nr = last.knlm.KNLMeansCL(d=d, a=a, s=s, h=h)
    logoM = mt_expand_multi(core.std.Expr([last, src], expr=['x y - abs 16 *']), mode='losange', sw=3, sh=3).std.Convolution(matrix=[1, 1, 1, 1, 0, 1, 1, 1, 1]).std.Deflate()
    clp_nr = core.std.MaskedMerge(last, clp_nr, logoM)
    if b_crop:
        clp_nr = Overlay(dlg, clp_nr, x=l, y=t)

    if dlg_orig is not None:
        clp_nr = core.std.ShufflePlanes([clp_nr, dlg_orig], planes=[0, 1, 2], colorfamily=dlg_orig.format.color_family)
    return clp_nr


# Vinverse: a small, but effective function against (residual) combing, by Didée
# sstr: strength of contra sharpening
# amnt: change no pixel by more than this (default=255: unrestricted)
# chroma: chroma mode, True=process chroma, False=pass chroma through
def Vinverse(clp, sstr=2.7, amnt=255, chroma=True):
    if not isinstance(clp, vs.VideoNode):
        raise vs.Error('Vinverse: This is not a clip')

    if clp.format.sample_type == vs.INTEGER:
        neutral = 1 << (clp.format.bits_per_sample - 1)
        peak = (1 << clp.format.bits_per_sample) - 1
    else:
        neutral = 0.0
        peak = 1.0

    if not chroma and clp.format.color_family != vs.GRAY:
        clp_orig = clp
        clp = mvf.GetPlane(clp, 0)
    else:
        clp_orig = None

    vblur = clp.std.Convolution(matrix=[50, 99, 50], mode='v')
    vblurD = core.std.MakeDiff(clp, vblur)
    vshrp = core.std.Expr([vblur, vblur.std.Convolution(matrix=[1, 4, 6, 4, 1], mode='v')], expr=[f'x x y - {sstr} * +'])
    vshrpD = core.std.MakeDiff(vshrp, vblur)
    expr = f'x {neutral} - y {neutral} - * 0 < x {neutral} - abs y {neutral} - abs < x y ? {neutral} - 0.25 * {neutral} + x {neutral} - abs y {neutral} - abs < x y ? ?'
    vlimD = core.std.Expr([vshrpD, vblurD], expr=[expr])
    last = core.std.MergeDiff(vblur, vlimD)
    if amnt <= 0:
        return clp
    elif amnt < 255:
        last = core.std.Expr([clp, last], expr=['x {AMN} + y < x {AMN} + x {AMN} - y > x {AMN} - y ? ?'.format(AMN=scale(amnt, peak))])

    if clp_orig is not None:
        last = core.std.ShufflePlanes([last, clp_orig], planes=[0, 1, 2], colorfamily=clp_orig.format.color_family)
    return last


def Vinverse2(clp, sstr=2.7, amnt=255, chroma=True):
    if not isinstance(clp, vs.VideoNode):
        raise vs.Error('Vinverse2: This is not a clip')

    if clp.format.sample_type == vs.INTEGER:
        neutral = 1 << (clp.format.bits_per_sample - 1)
        peak = (1 << clp.format.bits_per_sample) - 1
    else:
        neutral = 0.0
        peak = 1.0

    if not chroma and clp.format.color_family != vs.GRAY:
        clp_orig = clp
        clp = mvf.GetPlane(clp, 0)
    else:
        clp_orig = None

    vblur = sbrV(clp)
    vblurD = core.std.MakeDiff(clp, vblur)
    vshrp = core.std.Expr([vblur, vblur.std.Convolution(matrix=[1, 2, 1], mode='v')], expr=[f'x x y - {sstr} * +'])
    vshrpD = core.std.MakeDiff(vshrp, vblur)
    expr = f'x {neutral} - y {neutral} - * 0 < x {neutral} - abs y {neutral} - abs < x y ? {neutral} - 0.25 * {neutral} + x {neutral} - abs y {neutral} - abs < x y ? ?'
    vlimD = core.std.Expr([vshrpD, vblurD], expr=[expr])
    last = core.std.MergeDiff(vblur, vlimD)
    if amnt <= 0:
        return clp
    elif amnt < 255:
        last = core.std.Expr([clp, last], expr=['x {AMN} + y < x {AMN} + x {AMN} - y > x {AMN} - y ? ?'.format(AMN=scale(amnt, peak))])

    if clp_orig is not None:
        last = core.std.ShufflePlanes([last, clp_orig], planes=[0, 1, 2], colorfamily=clp_orig.format.color_family)
    return last


########################################################
#                                                      #
# LUTDeCrawl, a dot crawl removal script by Scintilla  #
# Created 10/3/08                                      #
# Last updated 10/3/08                                 #
#                                                      #
########################################################
#
# Requires YUV input, frame-based only.
# Is of average speed (faster than VagueDenoiser, slower than HQDN3D).
# Suggestions for improvement welcome: scintilla@aquilinestudios.org
#
# Arguments:
#
# ythresh (int, default=10) - This determines how close the luma values of the
#   pixel in the previous and next frames have to be for the pixel to
#   be hit.  Higher values (within reason) should catch more dot crawl,
#   but may introduce unwanted artifacts.  Probably shouldn't be set
#   above 20 or so.
#
# cthresh (int, default=10) - This determines how close the chroma values of the
#   pixel in the previous and next frames have to be for the pixel to
#   be hit.  Just as with ythresh.
#
# maxdiff (int, default=50) - This is the maximum difference allowed between the
#   luma values of the pixel in the CURRENT frame and in each of its
#   neighbour frames (so, the upper limit to what fluctuations are
#   considered dot crawl).  Lower values will reduce artifacts but may
#   cause the filter to miss some dot crawl.  Obviously, this should
#   never be lower than ythresh.  Meaningless if usemaxdiff = false.
#
# scnchg (int, default=25) - Scene change detection threshold.  Any frame with
#   total luma difference between it and the previous/next frame greater
#   than this value will not be processed.
#
# usemaxdiff (bool, default=True) - Whether or not to reject luma fluctuations
#   higher than maxdiff.  Setting this to false is not recommended, as
#   it may introduce artifacts; but on the other hand, it produces a
#   30% speed boost.  Test on your particular source.
#
# mask (bool, default=False) - When set true, the function will return the mask
#   instead of the image.  Use to find the best values of cthresh,
#   ythresh, and maxdiff.
#   (The scene change threshold, scnchg, is not reflected in the mask.)
#
###################
def LUTDeCrawl(input, ythresh=10, cthresh=10, maxdiff=50, scnchg=25, usemaxdiff=True, mask=False):
    def YDifferenceFromPrevious(n, f, clips):
        if f.props['_SceneChangePrev']:
            return clips[0]
        else:
            return clips[1]

    def YDifferenceToNext(n, f, clips):
        if f.props['_SceneChangeNext']:
            return clips[0]
        else:
            return clips[1]

    if not isinstance(input, vs.VideoNode) or input.format.color_family not in [vs.YUV, vs.YCOCG] or input.format.bits_per_sample > 10:
        raise vs.Error('LUTDeCrawl: This is not an 8-10 bit YUV or YCoCg clip')

    shift = input.format.bits_per_sample - 8
    peak = (1 << input.format.bits_per_sample) - 1

    ythresh = scale(ythresh, peak)
    cthresh = scale(cthresh, peak)
    maxdiff = scale(maxdiff, peak)

    input_minus = input.std.DuplicateFrames(frames=[0])
    input_plus = input.std.Trim(first=1) + input.std.Trim(first=input.num_frames - 1)

    input_y = mvf.GetPlane(input, 0)
    input_minus_y = mvf.GetPlane(input_minus, 0)
    input_minus_u = mvf.GetPlane(input_minus, 1)
    input_minus_v = mvf.GetPlane(input_minus, 2)
    input_plus_y = mvf.GetPlane(input_plus, 0)
    input_plus_u = mvf.GetPlane(input_plus, 1)
    input_plus_v = mvf.GetPlane(input_plus, 2)

    average_y = core.std.Expr([input_minus_y, input_plus_y], expr=[f'x y - abs {ythresh} < x y + 2 / 0 ?'])
    average_u = core.std.Expr([input_minus_u, input_plus_u], expr=[f'x y - abs {cthresh} < {peak} 0 ?'])
    average_v = core.std.Expr([input_minus_v, input_plus_v], expr=[f'x y - abs {cthresh} < {peak} 0 ?'])

    ymask = average_y.std.Binarize(threshold=1 << shift)
    if usemaxdiff:
        diffplus_y = core.std.Expr([input_plus_y, input_y], expr=[f'x y - abs {maxdiff} < {peak} 0 ?'])
        diffminus_y = core.std.Expr([input_minus_y, input_y], expr=[f'x y - abs {maxdiff} < {peak} 0 ?'])
        diffs_y = core.std.Lut2(diffplus_y, diffminus_y, function=lambda x, y: x & y)
        ymask = core.std.Lut2(ymask, diffs_y, function=lambda x, y: x & y)
    cmask = core.std.Lut2(average_u.std.Binarize(threshold=129 << shift), average_v.std.Binarize(threshold=129 << shift), function=lambda x, y: x & y)
    cmask = cmask.resize.Point(input.width, input.height)

    themask = core.std.Lut2(ymask, cmask, function=lambda x, y: x & y)

    fixed_y = core.std.Merge(average_y, input_y)

    output = core.std.ShufflePlanes([core.std.MaskedMerge(input_y, fixed_y, themask), input], planes=[0, 1, 2], colorfamily=input.format.color_family)

    input = SCDetect(input, threshold=scnchg / 255)
    output = output.std.FrameEval(eval=partial(YDifferenceFromPrevious, clips=[input, output]), prop_src=input)
    output = output.std.FrameEval(eval=partial(YDifferenceToNext, clips=[input, output]), prop_src=input)

    if mask:
        return themask
    else:
        return output


#####################################################
#                                                   #
# LUTDeRainbow, a derainbowing script by Scintilla  #
# Last updated 10/3/08                              #
#                                                   #
#####################################################
#
# Requires YUV input, frame-based only.
# Is of reasonable speed (faster than aWarpSharp, slower than DeGrainMedian).
# Suggestions for improvement welcome: scintilla@aquilinestudios.org
#
# Arguments:
#
# cthresh (int, default=10) - This determines how close the chroma values of the
#   pixel in the previous and next frames have to be for the pixel to
#   be hit.  Higher values (within reason) should catch more rainbows,
#   but may introduce unwanted artifacts.  Probably shouldn't be set
#   above 20 or so.
#
# ythresh (int, default=10) - If the y parameter is set true, then this
#   determines how close the luma values of the pixel in the previous
#   and next frames have to be for the pixel to be hit.  Just as with
#   cthresh.
#
# y (bool, default=True) - Determines whether luma difference will be considered
#   in determining which pixels to hit and which to leave alone.
#
# linkUV (bool, default=True) - Determines whether both chroma channels are
#   considered in determining which pixels in each channel to hit.
#   When set true, only pixels that meet the thresholds for both U and
#   V will be hit; when set false, the U and V channels are masked
#   separately (so a pixel could have its U hit but not its V, or vice
#   versa).
#
# mask (bool, default=False) - When set true, the function will return the mask
#   (for combined U/V) instead of the image.  Formerly used to find the
#   best values of cthresh and ythresh.  If linkUV=false, then this
#   mask won't actually be used anyway (because each chroma channel
#   will have its own mask).
#
###################
def LUTDeRainbow(input, cthresh=10, ythresh=10, y=True, linkUV=True, mask=False):
    if not isinstance(input, vs.VideoNode) or input.format.color_family not in [vs.YUV, vs.YCOCG] or input.format.bits_per_sample > 10:
        raise vs.Error('LUTDeRainbow: This is not an 8-10 bit YUV or YCoCg clip')

    shift = input.format.bits_per_sample - 8
    peak = (1 << input.format.bits_per_sample) - 1

    cthresh = scale(cthresh, peak)
    ythresh = scale(ythresh, peak)

    input_minus = input.std.DuplicateFrames(frames=[0])
    input_plus = input.std.Trim(first=1) + input.std.Trim(first=input.num_frames - 1)

    input_u = mvf.GetPlane(input, 1)
    input_v = mvf.GetPlane(input, 2)
    input_minus_y = mvf.GetPlane(input_minus, 0)
    input_minus_u = mvf.GetPlane(input_minus, 1)
    input_minus_v = mvf.GetPlane(input_minus, 2)
    input_plus_y = mvf.GetPlane(input_plus, 0)
    input_plus_u = mvf.GetPlane(input_plus, 1)
    input_plus_v = mvf.GetPlane(input_plus, 2)

    average_y = core.std.Expr([input_minus_y, input_plus_y], expr=[f'x y - abs {ythresh} < {peak} 0 ?']).resize.Bilinear(input_u.width, input_u.height)
    average_u = core.std.Expr([input_minus_u, input_plus_u], expr=[f'x y - abs {cthresh} < x y + 2 / 0 ?'])
    average_v = core.std.Expr([input_minus_v, input_plus_v], expr=[f'x y - abs {cthresh} < x y + 2 / 0 ?'])

    umask = average_u.std.Binarize(threshold=21 << shift)
    vmask = average_v.std.Binarize(threshold=21 << shift)
    themask = core.std.Lut2(umask, vmask, function=lambda x, y: x & y)
    if y:
        umask = core.std.Lut2(umask, average_y, function=lambda x, y: x & y)
        vmask = core.std.Lut2(vmask, average_y, function=lambda x, y: x & y)
        themask = core.std.Lut2(themask, average_y, function=lambda x, y: x & y)

    fixed_u = core.std.Merge(average_u, input_u)
    fixed_v = core.std.Merge(average_v, input_v)

    output_u = core.std.MaskedMerge(input_u, fixed_u, themask if linkUV else umask)
    output_v = core.std.MaskedMerge(input_v, fixed_v, themask if linkUV else vmask)

    output = core.std.ShufflePlanes([input, output_u, output_v], planes=[0, 0, 0], colorfamily=input.format.color_family)

    if mask:
        return themask.resize.Point(input.width, input.height)
    else:
        return output


##############################################################################
# Original script by g-force converted into a stand alone script by McCauley #
# latest version from December 10, 2008                                      #
##############################################################################
def Stab(clp, dxmax=4, dymax=4, mirror=0):
    if not isinstance(clp, vs.VideoNode):
        raise vs.Error('Stab: This is not a clip')

    temp = AverageFrames(clp, weights=[1] * 15, scenechange=25 / 255)
    inter = core.std.Interleave([core.rgvs.Repair(temp, AverageFrames(clp, weights=[1] * 3, scenechange=25 / 255), mode=[1]), clp])
    mdata = inter.mv.DepanEstimate(trust=0, dxmax=dxmax, dymax=dymax)
    last = inter.mv.DepanCompensate(data=mdata, offset=-1, mirror=mirror)
    return last[::2]


######
###
### GrainStabilizeMC v1.0      by mawen1250      2014.03.22
###
### Requirements: MVTools, RGVS
###
### Temporal-only on-top grain stabilizer
### Only stabilize the difference ( on-top grain ) between source clip and spatial-degrained clip
###
### Parameters:
###  nrmode (int)   - Mode to get grain/noise from input clip. 0: 3x3 Average Blur, 1: 3x3 SBR, 2: 5x5 SBR, 3: 7x7 SBR. Or define your own denoised clip "p". Default is 2 for HD / 1 for SD
###  radius (int)   - Temporal radius of MDegrain for grain stabilize (1-3). Default is 1
###  adapt (int)    - Threshold for luma-adaptative mask. -1: off, 0: source, 255: invert. Or define your own luma mask clip "Lmask". Default is -1
###  rep (int)      - Mode of repair to avoid artifacts, set 0 to turn off this operation. Default is 13
###  planes (int[]) - Whether to process the corresponding plane. The other planes will be passed through unchanged.
###
######
def GSMC(input, p=None, Lmask=None, nrmode=None, radius=1, adapt=-1, rep=13, planes=None, thSAD=300, thSADC=None, thSCD1=300, thSCD2=100, limit=None, limitc=None):
    if not isinstance(input, vs.VideoNode):
        raise vs.Error('GSMC: This is not a clip')

    if p is not None and (not isinstance(p, vs.VideoNode) or p.format.id != input.format.id):
        raise vs.Error("GSMC: 'p' must be the same format as input")

    if Lmask is not None and not isinstance(Lmask, vs.VideoNode):
        raise vs.Error("GSMC: 'Lmask' is not a clip")

    neutral = 1 << (input.format.bits_per_sample - 1)
    peak = (1 << input.format.bits_per_sample) - 1

    if planes is None:
        planes = list(range(input.format.num_planes))
    elif isinstance(planes, int):
        planes = [planes]

    HD = input.width > 1024 or input.height > 576

    if nrmode is None:
        nrmode = 2 if HD else 1
    if thSADC is None:
        thSADC = thSAD // 2
    if limit is not None:
        limit = scale(limit, peak)
    if limitc is not None:
        limitc = scale(limitc, peak)

    Y = 0 in planes
    U = 1 in planes
    V = 2 in planes

    chromamv = U or V
    blksize = 32 if HD else 16
    overlap = blksize // 4
    if not Y:
        if not U:
            plane = 2
        elif not V:
            plane = 1
        else:
            plane = 3
    elif not (U or V):
        plane = 0
    else:
        plane = 4

    # Kernel: Spatial Noise Dumping
    if p is not None:
        pre_nr = p
    elif nrmode <= 0:
        pre_nr = input.std.Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1], planes=planes)
    else:
        pre_nr = sbr(input, r=nrmode, planes=planes)
    dif_nr = core.std.MakeDiff(input, pre_nr, planes=planes)

    # Kernel: MC Grain Stabilize
    psuper = DitherLumaRebuild(pre_nr, s0=1, chroma=chromamv).mv.Super(pel=1, chroma=chromamv)
    difsuper = dif_nr.mv.Super(pel=1, levels=1, chroma=chromamv)

    analyse_args = dict(blksize=blksize, chroma=chromamv, truemotion=False, _global=True, overlap=overlap)
    fv1 = psuper.mv.Analyse(isb=False, delta=1, **analyse_args)
    bv1 = psuper.mv.Analyse(isb=True, delta=1, **analyse_args)
    if radius >= 2:
        fv2 = psuper.mv.Analyse(isb=False, delta=2, **analyse_args)
        bv2 = psuper.mv.Analyse(isb=True, delta=2, **analyse_args)
    if radius >= 3:
        fv3 = psuper.mv.Analyse(isb=False, delta=3, **analyse_args)
        bv3 = psuper.mv.Analyse(isb=True, delta=3, **analyse_args)

    degrain_args = dict(thsad=thSAD, thsadc=thSADC, plane=plane, limit=limit, limitc=limitc, thscd1=thSCD1, thscd2=thSCD2)
    if radius <= 1:
        dif_sb = core.mv.Degrain1(dif_nr, difsuper, bv1, fv1, **degrain_args)
    elif radius == 2:
        dif_sb = core.mv.Degrain2(dif_nr, difsuper, bv1, fv1, bv2, fv2, **degrain_args)
    else:
        dif_sb = core.mv.Degrain3(dif_nr, difsuper, bv1, fv1, bv2, fv2, bv3, fv3, **degrain_args)

    # Post-Process: Luma-Adaptive Mask Merging & Repairing
    stable = core.std.MergeDiff(pre_nr, dif_sb, planes=planes)
    if rep > 0:
        stable = core.rgvs.Repair(stable, input, mode=[rep if i in planes else 0 for i in range(input.format.num_planes)])

    if Lmask is not None:
        return core.std.MaskedMerge(input, stable, Lmask, planes=planes)
    elif adapt <= -1:
        return stable
    else:
        input_y = mvf.GetPlane(input, 0)
        if adapt == 0:
            Lmask = input_y.std.Convolution(matrix=[1, 1, 1, 1, 0, 1, 1, 1, 1])
        elif adapt >= 255:
            Lmask = input_y.std.Invert().std.Convolution(matrix=[1, 1, 1, 1, 0, 1, 1, 1, 1])
        else:
            expr = 'x {adapt} - abs {peak} * {adapt} {neutral} - abs {neutral} + /'.format(adapt=scale(adapt, peak), peak=peak, neutral=neutral)
            Lmask = input_y.std.Expr(expr=[expr]).std.Convolution(matrix=[1, 1, 1, 1, 0, 1, 1, 1, 1])
        return core.std.MaskedMerge(input, stable, Lmask, planes=planes)


####################################################################################################################################
###                                                                                                                              ###
###                                   Motion-Compensated Temporal Denoise: MCTemporalDenoise()                                   ###
###                                                                                                                              ###
###                                                     v1.4.20 by "LaTo INV."                                                   ###
###                                                                                                                              ###
###                                                           2 July 2010                                                        ###
###                                                                                                                              ###
####################################################################################################################################
###
###
###
### /!\ Needed filters: MVTools, DFTTest, FFT3DFilter, TTempSmooth, RGVS, Deblock, DCTFilter
### -------------------
###
###
###
### USAGE: MCTemporalDenoise(i, radius, pfMode, sigma, twopass, useTTmpSm, limit, limit2, post, chroma, refine,
###                          deblock, useQED, quant1, quant2,
###                          edgeclean, ECrad, ECthr,
###                          stabilize, maxr, TTstr,
###                          bwbh, owoh, blksize, overlap,
###                          bt, ncpu,
###                          thSAD, thSADC, thSAD2, thSADC2, thSCD1, thSCD2,
###                          truemotion, MVglobal, pel, pelsearch, search, searchparam, MVsharp, DCT,
###                          p, settings)
###
###
###
### PARAMETERS:
### -----------
###
### +---------+
### | DENOISE |
### +---------+--------------------------------------------------------------------------------------+
### | radius    : Temporal radius [1...6]                                                            |
### | pfMode    : Pre-filter mode [-1=off,0=FFT3DFilter,1=MinBlur(1),2=MinBlur(2),3=DFTTest]         |
### | sigma     : FFT3D sigma for the pre-filtering clip (if pfMode=0)                               |
### | twopass   : Do the denoising job in 2 stages (stronger but very slow)                          |
### | useTTmpSm : Use MDegrain (faster) or MCompensate+TTempSmooth (stronger)                        |
### | limit     : Limit the effect of the first denoising [-1=auto,0=off,1...255]                    |
### | limit2    : Limit the effect of the second denoising (if twopass=true) [-1=auto,0=off,1...255] |
### | post      : Sigma value for post-denoising with FFT3D [0=off,...]                              |
### | chroma    : Process or not the chroma plane                                                    |
### | refine    : Refine and recalculate motion data of previously estimated motion vectors          |
### +------------------------------------------------------------------------------------------------+
###
###
### +---------+
### | DEBLOCK |
### +---------+-----------------------------------------------------------------------------------+
### | deblock : Enable deblocking before the denoising                                            |
### | useQED  : If true, use Deblock_QED, else use Deblock (faster & stronger)                    |
### | quant1  : Deblock_QED "quant1" parameter (Deblock "quant" parameter is "(quant1+quant2)/2") |
### | quant2  : Deblock_QED "quant2" parameter (Deblock "quant" parameter is "(quant1+quant2)/2") |
### +---------------------------------------------------------------------------------------------+
###
###
### +------------------------------+
### | EDGECLEAN: DERING, DEHALO... |
### +------------------------------+-----------------------------------------------------------------------------------------------------+
### | edgeclean : Enable safe edgeclean process after the denoising (only on edges which are in non-detailed areas, so less detail loss) |
### | ECrad     : Radius for mask (the higher, the greater distance from the edge is filtered)                                           |
### | ECthr     : Threshold for mask (the higher, the less "small edges" are process) [0...255]                                          |
### +------------------------------------------------------------------------------------------------------------------------------------+
###
###
### +-----------+
### | STABILIZE |
### +-----------+------------------------------------------------------------------------------------------------+
### | stabilize : Enable TTempSmooth post processing to stabilize flat areas (background will be less "nervous") |
### | maxr      : Temporal radius (the higher, the more stable image)                                            |
### | TTstr     : Strength (see TTempSmooth docs)                                                                |
### +------------------------------------------------------------------------------------------------------------+
###
###
### +---------------------+
### | BLOCKSIZE / OVERLAP |
### +---------------------+----------------+
### | bwbh    : FFT3D blocksize            |
### | owoh    : FFT3D overlap              |
### |             - for speed:   bwbh/4    |
### |             - for quality: bwbh/2    |
### | blksize : MVTools blocksize          |
### | overlap : MVTools overlap            |
### |             - for speed:   blksize/4 |
### |             - for quality: blksize/2 |
### +--------------------------------------+
###
###
### +-------+
### | FFT3D |
### +-------+--------------------------+
### | bt   : FFT3D block temporal size |
### | ncpu : FFT3DFilter ncpu          |
### +----------------------------------+
###
###
### +---------+
### | MVTOOLS |
### +---------+------------------------------------------------------+
### | thSAD   : MVTools thSAD for the first pass                     |
### | thSADC  : MVTools thSADC for the first pass                    |
### | thSAD2  : MVTools thSAD for the second pass (if twopass=true)  |
### | thSADC2 : MVTools thSADC for the second pass (if twopass=true) |
### | thSCD1  : MVTools thSCD1                                       |
### | thSCD2  : MVTools thSCD2                                       |
### +-----------------------------------+----------------------------+
### | truemotion  : MVTools truemotion  |
### | MVglobal    : MVTools global      |
### | pel         : MVTools pel         |
### | pelsearch   : MVTools pelsearch   |
### | search      : MVTools search      |
### | searchparam : MVTools searchparam |
### | MVsharp     : MVTools sharp       |
### | DCT         : MVTools DCT         |
### +-----------------------------------+
###
###
### +--------+
### | GLOBAL |
### +--------+-----------------------------------------------------+
### | p        : Set an external prefilter clip                    |
### | settings : Global MCTemporalDenoise settings [default="low"] |
### |             - "very low"                                     |
### |             - "low"                                          |
### |             - "medium"                                       |
### |             - "high"                                         |
### |             - "very high"                                    |
### +--------------------------------------------------------------+
###
###
###
### DEFAULTS:
### ---------
###
### +-------------+----------------------+----------------------+----------------------+----------------------+----------------------+
### | SETTINGS    |      VERY LOW        |      LOW             |      MEDIUM          |      HIGH            |      VERY HIGH       |
### |-------------+----------------------+----------------------+----------------------+----------------------+----------------------|
### | radius      |      1               |      2               |      3               |      2               |      3               |
### | pfMode      |      3               |      3               |      3               |      3               |      3               |
### | sigma       |      2               |      4               |      8               |      12              |      16              |
### | twopass     |      false           |      false           |      false           |      true            |      true            |
### | useTTmpSm   |      false           |      false           |      false           |      false           |      false           |
### | limit       |      -1              |      -1              |      -1              |      -1              |      0               |
### | limit2      |      -1              |      -1              |      -1              |      0               |      0               |
### | post        |      0               |      0               |      0               |      0               |      0               |
### | chroma      |      false           |      false           |      true            |      true            |      true            |
### |-------------+----------------------+----------------------+----------------------+----------------------+----------------------|
### | deblock     |      false           |      false           |      false           |      false           |      false           |
### | useQED      |      true            |      true            |      true            |      false           |      false           |
### | quant1      |      10              |      20              |      30              |      30              |      40              |
### | quant2      |      20              |      40              |      60              |      60              |      80              |
### |-------------+----------------------+----------------------+----------------------+----------------------+----------------------|
### | edgeclean   |      false           |      false           |      false           |      false           |      false           |
### | ECrad       |      1               |      2               |      3               |      4               |      5               |
### | ECthr       |      64              |      32              |      32              |      16              |      16              |
### |-------------+----------------------+----------------------+----------------------+----------------------+----------------------|
### | stabilize   |      false           |      false           |      false           |      true            |      true            |
### | maxr        |      1               |      1               |      2               |      2               |      2               |
### | TTstr       |      1               |      1               |      1               |      2               |      2               |
### |-------------+----------------------+----------------------+----------------------+----------------------+----------------------|
### | bwbh        |      HD?16:8         |      HD?16:8         |      HD?16:8         |      HD?16:8         |      HD?16:8         |
### | owoh        |      HD? 8:4         |      HD? 8:4         |      HD? 8:4         |      HD? 8:4         |      HD? 8:4         |
### | blksize     |      HD?16:8         |      HD?16:8         |      HD?16:8         |      HD?16:8         |      HD?16:8         |
### | overlap     |      HD? 8:4         |      HD? 8:4         |      HD? 8:4         |      HD? 8:4         |      HD? 8:4         |
### |-------------+----------------------+----------------------+----------------------+----------------------+----------------------|
### | bt          |      1               |      3               |      3               |      3               |      4               |
### | ncpu        |      1               |      1               |      1               |      1               |      1               |
### |-------------+----------------------+----------------------+----------------------+----------------------+----------------------|
### | thSAD       |      200             |      300             |      400             |      500             |      600             |
### | thSADC      |      thSAD/2         |      thSAD/2         |      thSAD/2         |      thSAD/2         |      thSAD/2         |
### | thSAD2      |      200             |      300             |      400             |      500             |      600             |
### | thSADC2     |      thSAD2/2        |      thSAD2/2        |      thSAD2/2        |      thSAD2/2        |      thSAD2/2        |
### | thSCD1      |      200             |      300             |      400             |      500             |      600             |
### | thSCD2      |      90              |      100             |      100             |      130             |      130             |
### |-------------+----------------------+----------------------+----------------------+----------------------+----------------------|
### | truemotion  |      false           |      false           |      false           |      false           |      false           |
### | MVglobal    |      true            |      true            |      true            |      true            |      true            |
### | pel         |      1               |      2               |      2               |      2               |      2               |
### | pelsearch   |      1               |      2               |      2               |      2               |      2               |
### | search      |      4               |      4               |      4               |      4               |      4               |
### | searchparam |      2               |      2               |      2               |      2               |      2               |
### | MVsharp     |      2               |      2               |      2               |      1               |      0               |
### | DCT         |      0               |      0               |      0               |      0               |      0               |
### +-------------+----------------------+----------------------+----------------------+----------------------+----------------------+
###
####################################################################################################################################
def MCTemporalDenoise(i, radius=None, pfMode=3, sigma=None, twopass=None, useTTmpSm=False, limit=None, limit2=None, post=0, chroma=None, refine=False, deblock=False, useQED=None, quant1=None,
                      quant2=None, edgeclean=False, ECrad=None, ECthr=None, stabilize=None, maxr=None, TTstr=None, bwbh=None, owoh=None, blksize=None, overlap=None, bt=None, ncpu=1, thSAD=None,
                      thSADC=None, thSAD2=None, thSADC2=None, thSCD1=None, thSCD2=None, truemotion=False, MVglobal=True, pel=None, pelsearch=None, search=4, searchparam=2, MVsharp=None, DCT=0, p=None,
                      settings='low'):
    if not isinstance(i, vs.VideoNode):
        raise vs.Error('MCTemporalDenoise: This is not a clip')

    if p is not None and (not isinstance(p, vs.VideoNode) or p.format.id != i.format.id):
        raise vs.Error("MCTemporalDenoise: 'p' must be the same format as input")

    isGray = (i.format.color_family == vs.GRAY)

    neutral = 1 << (i.format.bits_per_sample - 1)
    peak = (1 << i.format.bits_per_sample) - 1

    ### DEFAULTS
    try:
        settings_num = ['very low', 'low', 'medium', 'high', 'very high'].index(settings.lower())
    except:
        raise vs.Error('MCTemporalDenoise: These settings do not exist')

    HD = i.width > 1024 or i.height > 576

    if radius is None:
        radius = [1, 2, 3, 2, 3][settings_num]
    if sigma is None:
        sigma = [2, 4, 8, 12, 16][settings_num]
    if twopass is None:
        twopass = [False, False, False, True, True][settings_num]
    if limit is None:
        limit = [-1, -1, -1, -1, 0][settings_num]
    if limit2 is None:
        limit2 = [-1, -1, -1, 0, 0][settings_num]
    if chroma is None:
        chroma = [False, False, True, True, True][settings_num]
    if useQED is None:
        useQED = [True, True, True, False, False][settings_num]
    if quant1 is None:
        quant1 = [10, 20, 30, 30, 40][settings_num]
    if quant2 is None:
        quant2 = [20, 40, 60, 60, 80][settings_num]
    if ECrad is None:
        ECrad = [1, 2, 3, 4, 5][settings_num]
    if ECthr is None:
        ECthr = [64, 32, 32, 16, 16][settings_num]
    if stabilize is None:
        stabilize = [False, False, False, True, True][settings_num]
    if maxr is None:
        maxr = [1, 1, 2, 2, 2][settings_num]
    if TTstr is None:
        TTstr = [1, 1, 1, 2, 2][settings_num]
    if bwbh is None:
        bwbh = 16 if HD else 8
    if owoh is None:
        owoh = 8 if HD else 4
    if blksize is None:
        blksize = 16 if HD else 8
    if overlap is None:
        overlap = 8 if HD else 4
    if bt is None:
        bt = [1, 3, 3, 3, 4][settings_num]
    if thSAD is None:
        thSAD = [200, 300, 400, 500, 600][settings_num]
    if thSADC is None:
        thSADC = thSAD // 2
    if thSAD2 is None:
        thSAD2 = [200, 300, 400, 500, 600][settings_num]
    if thSADC2 is None:
        thSADC2 = thSAD2 // 2
    if thSCD1 is None:
        thSCD1 = [200, 300, 400, 500, 600][settings_num]
    if thSCD2 is None:
        thSCD2 = [90, 100, 100, 130, 130][settings_num]
    if pel is None:
        pel = [1, 2, 2, 2, 2][settings_num]
    if pelsearch is None:
        pelsearch = [1, 2, 2, 2, 2][settings_num]
    if MVsharp is None:
        MVsharp = [2, 2, 2, 1, 0][settings_num]

    sigma *= peak / 255
    limit = scale(limit, peak)
    limit2 = scale(limit2, peak)
    post *= peak / 255
    ECthr = scale(ECthr, peak)
    planes = [0, 1, 2] if chroma and not isGray else [0]

    ### INPUT
    mod = bwbh if bwbh >= blksize else blksize
    xi = i.width
    xf = math.ceil(xi / mod) * mod - xi + mod
    xn = int(xi + xf)
    yi = i.height
    yf = math.ceil(yi / mod) * mod - yi + mod
    yn = int(yi + yf)

    pointresize_args = dict(width=xn, height=yn, src_left=-xf / 2, src_top=-yf / 2, src_width=xn, src_height=yn)
    i = i.resize.Point(**pointresize_args)

    ### PREFILTERING
    fft3d_args = dict(planes=planes, bw=bwbh, bh=bwbh, bt=bt, ow=owoh, oh=owoh, ncpu=ncpu)
    if p is not None:
        p = p.resize.Point(**pointresize_args)
    elif pfMode <= -1:
        p = i
    elif pfMode == 0:
        p = i.fft3dfilter.FFT3DFilter(sigma=sigma * 0.8, sigma2=sigma * 0.6, sigma3=sigma * 0.4, sigma4=sigma * 0.2, **fft3d_args)
    elif pfMode >= 3:
        p = i.dfttest.DFTTest(tbsize=1, slocation=[0.0,4.0, 0.2,9.0, 1.0,15.0], planes=planes)
    else:
        p = MinBlur(i, r=pfMode, planes=planes)

    pD = core.std.MakeDiff(i, p, planes=planes)
    p = DitherLumaRebuild(p, s0=1, chroma=chroma)

    ### DEBLOCKING
    crop_args = dict(left=xf // 2, right=xf // 2, top=yf // 2, bottom=yf // 2)
    if not deblock:
        d = i
    elif useQED:
        d = Deblock_QED(i.std.Crop(**crop_args), quant1=quant1, quant2=quant2, uv=3 if chroma else 2).resize.Point(**pointresize_args)
    else:
        d = i.std.Crop(**crop_args).deblock.Deblock(quant=(quant1 + quant2) // 2, planes=planes).resize.Point(**pointresize_args)

    ### PREPARING
    super_args = dict(hpad=0, vpad=0, pel=pel, chroma=chroma, sharp=MVsharp)
    pMVS = p.mv.Super(rfilter=4 if refine else 2, **super_args)
    if refine:
        rMVS = p.mv.Super(levels=1, **super_args)

    analyse_args = dict(blksize=blksize, search=search, searchparam=searchparam, pelsearch=pelsearch, chroma=chroma, truemotion=truemotion, _global=MVglobal, overlap=overlap, dct=DCT)
    recalculate_args = dict(thsad=thSAD // 2, blksize=max(blksize // 2, 4), search=search, chroma=chroma, truemotion=truemotion, overlap=max(overlap // 2, 2), dct=DCT)
    f1v = pMVS.mv.Analyse(isb=False, delta=1, **analyse_args)
    b1v = pMVS.mv.Analyse(isb=True, delta=1, **analyse_args)
    if refine:
        f1v = core.mv.Recalculate(rMVS, f1v, **recalculate_args)
        b1v = core.mv.Recalculate(rMVS, b1v, **recalculate_args)
    if radius > 1:
        f2v = pMVS.mv.Analyse(isb=False, delta=2, **analyse_args)
        b2v = pMVS.mv.Analyse(isb=True, delta=2, **analyse_args)
        if refine:
            f2v = core.mv.Recalculate(rMVS, f2v, **recalculate_args)
            b2v = core.mv.Recalculate(rMVS, b2v, **recalculate_args)
    if radius > 2:
        f3v = pMVS.mv.Analyse(isb=False, delta=3, **analyse_args)
        b3v = pMVS.mv.Analyse(isb=True, delta=3, **analyse_args)
        if refine:
            f3v = core.mv.Recalculate(rMVS, f3v, **recalculate_args)
            b3v = core.mv.Recalculate(rMVS, b3v, **recalculate_args)
    if radius > 3:
        f4v = pMVS.mv.Analyse(isb=False, delta=4, **analyse_args)
        b4v = pMVS.mv.Analyse(isb=True, delta=4, **analyse_args)
        if refine:
            f4v = core.mv.Recalculate(rMVS, f4v, **recalculate_args)
            b4v = core.mv.Recalculate(rMVS, b4v, **recalculate_args)
    if radius > 4:
        f5v = pMVS.mv.Analyse(isb=False, delta=5, **analyse_args)
        b5v = pMVS.mv.Analyse(isb=True, delta=5, **analyse_args)
        if refine:
            f5v = core.mv.Recalculate(rMVS, f5v, **recalculate_args)
            b5v = core.mv.Recalculate(rMVS, b5v, **recalculate_args)
    if radius > 5:
        f6v = pMVS.mv.Analyse(isb=False, delta=6, **analyse_args)
        b6v = pMVS.mv.Analyse(isb=True, delta=6, **analyse_args)
        if refine:
            f6v = core.mv.Recalculate(rMVS, f6v, **recalculate_args)
            b6v = core.mv.Recalculate(rMVS, b6v, **recalculate_args)

    # if useTTmpSm or stabilize:
        # mask_args = dict(ml=thSAD, gamma=0.999, kind=1, ysc=255)
        # SAD_f1m = core.mv.Mask(d, f1v, **mask_args)
        # SAD_b1m = core.mv.Mask(d, b1v, **mask_args)

    def MCTD_MVD(i, iMVS, thSAD, thSADC):
        degrain_args = dict(thsad=thSAD, thsadc=thSADC, plane=4 if chroma else 0, thscd1=thSCD1, thscd2=thSCD2)
        if radius <= 1:
            sm = core.mv.Degrain1(i, iMVS, b1v, f1v, **degrain_args)
        elif radius == 2:
            sm = core.mv.Degrain2(i, iMVS, b1v, f1v, b2v, f2v, **degrain_args)
        elif radius == 3:
            sm = core.mv.Degrain3(i, iMVS, b1v, f1v, b2v, f2v, b3v, f3v, **degrain_args)
        elif radius == 4:
            mv12 = core.mv.Degrain2(i, iMVS, b1v, f1v, b2v, f2v, **degrain_args)
            mv34 = core.mv.Degrain2(i, iMVS, b3v, f3v, b4v, f4v, **degrain_args)
            sm = core.std.Merge(mv12, mv34, weight=[0.4444])
        elif radius == 5:
            mv123 = core.mv.Degrain3(i, iMVS, b1v, f1v, b2v, f2v, b3v, f3v, **degrain_args)
            mv45 = core.mv.Degrain2(i, iMVS, b4v, f4v, b5v, f5v, **degrain_args)
            sm = core.std.Merge(mv123, mv45, weight=[0.4545])
        else:
            mv123 = core.mv.Degrain3(i, iMVS, b1v, f1v, b2v, f2v, b3v, f3v, **degrain_args)
            mv456 = core.mv.Degrain3(i, iMVS, b4v, f4v, b5v, f5v, b6v, f6v, **degrain_args)
            sm = core.std.Merge(mv123, mv456, weight=[0.4615])

        return sm

    def MCTD_TTSM(i, iMVS, thSAD):
        compensate_args = dict(thsad=thSAD, thscd1=thSCD1, thscd2=thSCD2)
        f1c = core.mv.Compensate(i, iMVS, f1v, **compensate_args)
        b1c = core.mv.Compensate(i, iMVS, b1v, **compensate_args)
        if radius > 1:
            f2c = core.mv.Compensate(i, iMVS, f2v, **compensate_args)
            b2c = core.mv.Compensate(i, iMVS, b2v, **compensate_args)
            # SAD_f2m = core.mv.Mask(i, f2v, **mask_args)
            # SAD_b2m = core.mv.Mask(i, b2v, **mask_args)
        if radius > 2:
            f3c = core.mv.Compensate(i, iMVS, f3v, **compensate_args)
            b3c = core.mv.Compensate(i, iMVS, b3v, **compensate_args)
            # SAD_f3m = core.mv.Mask(i, f3v, **mask_args)
            # SAD_b3m = core.mv.Mask(i, b3v, **mask_args)
        if radius > 3:
            f4c = core.mv.Compensate(i, iMVS, f4v, **compensate_args)
            b4c = core.mv.Compensate(i, iMVS, b4v, **compensate_args)
            # SAD_f4m = core.mv.Mask(i, f4v, **mask_args)
            # SAD_b4m = core.mv.Mask(i, b4v, **mask_args)
        if radius > 4:
            f5c = core.mv.Compensate(i, iMVS, f5v, **compensate_args)
            b5c = core.mv.Compensate(i, iMVS, b5v, **compensate_args)
            # SAD_f5m = core.mv.Mask(i, f5v, **mask_args)
            # SAD_b5m = core.mv.Mask(i, b5v, **mask_args)
        if radius > 5:
            f6c = core.mv.Compensate(i, iMVS, f6v, **compensate_args)
            b6c = core.mv.Compensate(i, iMVS, b6v, **compensate_args)
            # SAD_f6m = core.mv.Mask(i, f6v, **mask_args)
            # SAD_b6m = core.mv.Mask(i, b6v, **mask_args)

        # b = i.std.BlankClip(color=[0] if isGray else [0, neutral, neutral])
        if radius <= 1:
            c = core.std.Interleave([f1c, i, b1c])
            # SAD_m = core.std.Interleave([SAD_f1m, b, SAD_b1m])
        elif radius == 2:
            c = core.std.Interleave([f2c, f1c, i, b1c, b2c])
            # SAD_m = core.std.Interleave([SAD_f2m, SAD_f1m, b, SAD_b1m, SAD_b2m])
        elif radius == 3:
            c = core.std.Interleave([f3c, f2c, f1c, i, b1c, b2c, b3c])
            # SAD_m = core.std.Interleave([SAD_f3m, SAD_f2m, SAD_f1m, b, SAD_b1m, SAD_b2m, SAD_b3m])
        elif radius == 4:
            c = core.std.Interleave([f4c, f3c, f2c, f1c, i, b1c, b2c, b3c, b4c])
            # SAD_m = core.std.Interleave([SAD_f4m, SAD_f3m, SAD_f2m, SAD_f1m, b, SAD_b1m, SAD_b2m, SAD_b3m, SAD_b4m])
        elif radius == 5:
            c = core.std.Interleave([f5c, f4c, f3c, f2c, f1c, i, b1c, b2c, b3c, b4c, b5c])
            # SAD_m = core.std.Interleave([SAD_f5m, SAD_f4m, SAD_f3m, SAD_f2m, SAD_f1m, b, SAD_b1m, SAD_b2m, SAD_b3m, SAD_b4m, SAD_b5m])
        else:
            c = core.std.Interleave([f6c, f5c, f4c, f3c, f2c, f1c, i, b1c, b2c, b3c, b4c, b5c, b6c])
            # SAD_m = core.std.Interleave([SAD_f6m, SAD_f5m, SAD_f4m, SAD_f3m, SAD_f2m, SAD_f1m, b, SAD_b1m, SAD_b2m, SAD_b3m, SAD_b4m, SAD_b5m, SAD_b6m])

        # sm = c.ttmpsm.TTempSmooth(maxr=radius, thresh=[255], mdiff=[1], strength=radius + 1, scthresh=99.9, fp=False, pfclip=SAD_m, planes=planes)
        sm = c.ttmpsm.TTempSmooth(maxr=radius, thresh=[255], mdiff=[1], strength=radius + 1, scthresh=99.9, fp=False, planes=planes)
        return sm.std.SelectEvery(cycle=radius * 2 + 1, offsets=[radius])

    ### DENOISING: FIRST PASS
    dMVS = d.mv.Super(levels=1, **super_args)
    sm = MCTD_TTSM(d, dMVS, thSAD) if useTTmpSm else MCTD_MVD(d, dMVS, thSAD, thSADC)

    if limit <= -1:
        smD = core.std.MakeDiff(i, sm, planes=planes)
        expr = f'x {neutral} - abs y {neutral} - abs < x y ?'
        DD = core.std.Expr([pD, smD], expr=[expr] if chroma or isGray else [expr, ''])
        smL = core.std.MakeDiff(i, DD, planes=planes)
    elif limit > 0:
        expr = f'x y - abs {limit} <= x x y - 0 < y {limit} - y {limit} + ? ?'
        smL = core.std.Expr([sm, i], expr=[expr] if chroma or isGray else [expr, ''])
    else:
        smL = sm

    ### DENOISING: SECOND PASS
    if twopass:
        smLMVS = smL.mv.Super(levels=1, **super_args)
        sm = MCTD_TTSM(smL, smLMVS, thSAD2) if useTTmpSm else MCTD_MVD(smL, smLMVS, thSAD2, thSADC2)

        if limit2 <= -1:
            smD = core.std.MakeDiff(i, sm, planes=planes)
            expr = f'x {neutral} - abs y {neutral} - abs < x y ?'
            DD = core.std.Expr([pD, smD], expr=[expr] if chroma or isGray else [expr, ''])
            smL = core.std.MakeDiff(i, DD, planes=planes)
        elif limit2 > 0:
            expr = f'x y - abs {limit2} <= x x y - 0 < y {limit2} - y {limit2} + ? ?'
            smL = core.std.Expr([sm, i], expr=[expr] if chroma or isGray else [expr, ''])
        else:
            smL = sm

    ### POST-DENOISING: FFT3D
    if post <= 0:
        smP = smL
    else:
        smP = smL.fft3dfilter.FFT3DFilter(sigma=post * 0.8, sigma2=post * 0.6, sigma3=post * 0.4, sigma4=post * 0.2, **fft3d_args)

    ### EDGECLEANING
    if edgeclean:
        mP = AvsPrewitt(mvf.GetPlane(smP, 0))
        mS = mt_expand_multi(mP, sw=ECrad, sh=ECrad).std.Inflate()
        mD = core.std.Expr([mS, mP.std.Inflate()], expr=[f'x y - {ECthr} <= 0 x y - ?']).std.Inflate().std.Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1])
        smP = core.std.MaskedMerge(smP, DeHalo_alpha(smP.dfttest.DFTTest(tbsize=1, planes=planes), darkstr=0), mD, planes=planes)

    ### STABILIZING
    if stabilize:
        # mM = core.std.Merge(mvf.GetPlane(SAD_f1m, 0), mvf.GetPlane(SAD_b1m, 0)).std.Lut(function=lambda x: min(cround(x ** 1.6), peak))
        mE = AvsPrewitt(mvf.GetPlane(smP, 0)).std.Lut(function=lambda x: min(cround(x ** 1.8), peak)).std.Median().std.Inflate()
        # mF = core.std.Expr([mM, mE], expr=['x y max']).std.Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1])
        mF = mE.std.Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1])
        TTc = smP.ttmpsm.TTempSmooth(maxr=maxr, mdiff=[255], strength=TTstr, planes=planes)
        smP = core.std.MaskedMerge(TTc, smP, mF, planes=planes)

    ### OUTPUT
    return smP.std.Crop(**crop_args)


################################################################################################
###                                                                                          ###
###                           Simple MDegrain Mod - SMDegrain()                              ###
###                                                                                          ###
###                       Mod by Dogway - Original idea by Caroliano                         ###
###                                                                                          ###
###          Special Thanks: Sagekilla, Didée, cretindesalpes, Gavino and MVtools people     ###
###                                                                                          ###
###                       v3.1.2d (Dogway's mod) - 21 July 2015                              ###
###                                                                                          ###
################################################################################################
###
### General purpose simple degrain function. Pure temporal denoiser. Basically a wrapper(function)/frontend of mvtools2+mdegrain
### with some added common related options. Goal is accessibility and quality but not targeted to any specific kind of source.
### The reason behind is to keep it simple so aside masktools2 you will only need MVTools2.
###
### Check documentation for deep explanation on settings and defaults.
### VideoHelp thread: (http://forum.videohelp.com/threads/369142)
###
################################################################################################

# Globals
bv6 = bv4 = bv3 = bv2 = bv1 = fv1 = fv2 = fv3 = fv4 = fv6 = None

def SMDegrain(input, tr=2, thSAD=300, thSADC=None, RefineMotion=False, contrasharp=None, CClip=None, interlaced=False, tff=None, plane=4, Globals=0, pel=None, subpixel=2, prefilter=-1, mfilter=None,
              blksize=None, overlap=None, search=4, truemotion=None, MVglobal=None, dct=0, limit=255, limitc=None, thSCD1=400, thSCD2=130, chroma=True, hpad=None, vpad=None, Str=1.0, Amp=0.0625):
    if not isinstance(input, vs.VideoNode):
        raise vs.Error('SMDegrain: This is not a clip')

    if input.format.color_family == vs.GRAY:
        plane = 0
        chroma = False

    peak = (1 << input.format.bits_per_sample) - 1

    # Defaults & Conditionals
    thSAD2 = thSAD // 2
    if thSADC is None:
        thSADC = thSAD2

    GlobalR = (Globals == 1)
    GlobalO = (Globals >= 3)
    if1 = CClip is not None

    if contrasharp is None:
        contrasharp = not GlobalO and if1

    w = input.width
    h = input.height
    preclip = isinstance(prefilter, vs.VideoNode)
    ifC = isinstance(contrasharp, bool)
    if0 = contrasharp if ifC else contrasharp > 0
    if4 = w > 1024 or h > 576

    if pel is None:
        pel = 1 if if4 else 2
    if pel < 2:
        subpixel = min(subpixel, 2)
    pelclip = pel > 1 and subpixel >= 3

    if blksize is None:
        blksize = 16 if if4 else 8
    blk2 = blksize // 2
    if overlap is None:
        overlap = blk2
    ovl2 = overlap // 2
    if truemotion is None:
        truemotion = not if4
    if MVglobal is None:
        MVglobal = truemotion

    planes = [0, 1, 2] if chroma else [0]
    plane0 = (plane != 0)

    if hpad is None:
        hpad = blksize
    if vpad is None:
        vpad = blksize
    limit = scale(limit, peak)
    if limitc is None:
        limitc = limit
    else:
        limitc = scale(limitc, peak)

    # Error Report
    if not (ifC or isinstance(contrasharp, int)):
        raise vs.Error("SMDegrain: 'contrasharp' only accepts bool and integer inputs")
    if if1 and (not isinstance(CClip, vs.VideoNode) or CClip.format.id != input.format.id):
        raise vs.Error("SMDegrain: 'CClip' must be the same format as input")
    if interlaced and h & 3:
        raise vs.Error('SMDegrain: Interlaced source requires mod 4 height sizes')
    if interlaced and not isinstance(tff, bool):
        raise vs.Error("SMDegrain: 'tff' must be set if source is interlaced. Setting tff to true means top field first and false means bottom field first")
    if not (isinstance(prefilter, int) or preclip):
        raise vs.Error("SMDegrain: 'prefilter' only accepts integer and clip inputs")
    if preclip and prefilter.format.id != input.format.id:
        raise vs.Error("SMDegrain: 'prefilter' must be the same format as input")
    if mfilter is not None and (not isinstance(mfilter, vs.VideoNode) or mfilter.format.id != input.format.id):
        raise vs.Error("SMDegrain: 'mfilter' must be the same format as input")
    if RefineMotion and blksize < 8:
        raise vs.Error('SMDegrain: For RefineMotion you need a blksize of at least 8')
    if not chroma and plane != 0:
        raise vs.Error('SMDegrain: Denoising chroma with luma only vectors is bugged in mvtools and thus unsupported')

    # RefineMotion Variables
    if RefineMotion:
        halfblksize = blk2                                         # MRecalculate works with half block size
        halfoverlap = overlap if overlap <= 2 else ovl2 + ovl2 % 2 # Halve the overlap to suit the halved block size
        halfthSAD = thSAD2                                         # MRecalculate uses a more strict thSAD, which defaults to 150 (half of function's default of 300)

    # Input preparation for Interlacing
    if not interlaced:
        inputP = input
    else:
        inputP = input.std.SeparateFields(tff=tff)

    # Prefilter & Motion Filter
    if mfilter is None:
        mfilter = inputP

    if not GlobalR:
        if preclip:
            pref = prefilter
        elif prefilter <= -1:
            pref = inputP
        elif prefilter == 3:
            expr = 'x {i} < {peak} x {j} > 0 {peak} x {i} - {peak} {j} {i} - / * - ? ?'.format(i=scale(16, peak), j=scale(75, peak), peak=peak)
            pref = core.std.MaskedMerge(inputP.dfttest.DFTTest(tbsize=1, slocation=[0.0,4.0, 0.2,9.0, 1.0,15.0], planes=planes),
                                        inputP,
                                        mvf.GetPlane(inputP, 0).std.Expr(expr=[expr]),
                                        planes=planes)
        elif prefilter >= 4:
            if chroma:
                pref = KNLMeansCL(inputP, d=1, a=1, h=7)
            else:
                pref = inputP.knlm.KNLMeansCL(d=1, a=1, h=7)
        else:
            pref = MinBlur(inputP, r=prefilter, planes=planes)
    else:
        pref = inputP

    # Default Auto-Prefilter - Luma expansion TV->PC (up to 16% more values for motion estimation)
    if not GlobalR:
        pref = DitherLumaRebuild(pref, s0=Str, c=Amp, chroma=chroma)

    # Subpixel 3
    if pelclip:
        import nnedi3_resample as nnrs
        cshift = 0.25 if pel == 2 else 0.375
        pclip = nnrs.nnedi3_resample(pref, w * pel, h * pel, src_left=cshift, src_top=cshift, nns=4)
        if not GlobalR:
            pclip2 = nnrs.nnedi3_resample(inputP, w * pel, h * pel, src_left=cshift, src_top=cshift, nns=4)

    # Motion vectors search
    global bv6, bv4, bv3, bv2, bv1, fv1, fv2, fv3, fv4, fv6
    super_args = dict(hpad=hpad, vpad=vpad, pel=pel)
    analyse_args = dict(blksize=blksize, search=search, chroma=chroma, truemotion=truemotion, _global=MVglobal, overlap=overlap, dct=dct)
    if RefineMotion:
        recalculate_args = dict(thsad=halfthSAD, blksize=halfblksize, search=search, chroma=chroma, truemotion=truemotion, overlap=halfoverlap, dct=dct)

    if pelclip:
        super_search = pref.mv.Super(chroma=chroma, rfilter=4, pelclip=pclip, **super_args)
    else:
        super_search = pref.mv.Super(chroma=chroma, sharp=subpixel, rfilter=4, **super_args)

    if not GlobalR:
        if pelclip:
            super_render = inputP.mv.Super(levels=1, chroma=plane0, pelclip=pclip2, **super_args)
            if RefineMotion:
                Recalculate = pref.mv.Super(levels=1, chroma=chroma, pelclip=pclip, **super_args)
        else:
            super_render = inputP.mv.Super(levels=1, chroma=plane0, sharp=subpixel, **super_args)
            if RefineMotion:
                Recalculate = pref.mv.Super(levels=1, chroma=chroma, sharp=subpixel, **super_args)

        if interlaced:
            if tr > 2:
                bv6 = super_search.mv.Analyse(isb=True, delta=6, **analyse_args)
                fv6 = super_search.mv.Analyse(isb=False, delta=6, **analyse_args)
                if RefineMotion:
                    bv6 = core.mv.Recalculate(Recalculate, bv6, **recalculate_args)
                    fv6 = core.mv.Recalculate(Recalculate, fv6, **recalculate_args)
            if tr > 1:
                bv4 = super_search.mv.Analyse(isb=True, delta=4, **analyse_args)
                fv4 = super_search.mv.Analyse(isb=False, delta=4, **analyse_args)
                if RefineMotion:
                    bv4 = core.mv.Recalculate(Recalculate, bv4, **recalculate_args)
                    fv4 = core.mv.Recalculate(Recalculate, fv4, **recalculate_args)
        else:
            if tr > 2:
                bv3 = super_search.mv.Analyse(isb=True, delta=3, **analyse_args)
                fv3 = super_search.mv.Analyse(isb=False, delta=3, **analyse_args)
                if RefineMotion:
                    bv3 = core.mv.Recalculate(Recalculate, bv3, **recalculate_args)
                    fv3 = core.mv.Recalculate(Recalculate, fv3, **recalculate_args)
            bv1 = super_search.mv.Analyse(isb=True, delta=1, **analyse_args)
            fv1 = super_search.mv.Analyse(isb=False, delta=1, **analyse_args)
            if RefineMotion:
                bv1 = core.mv.Recalculate(Recalculate, bv1, **recalculate_args)
                fv1 = core.mv.Recalculate(Recalculate, fv1, **recalculate_args)
        if interlaced or tr > 1:
            bv2 = super_search.mv.Analyse(isb=True, delta=2, **analyse_args)
            fv2 = super_search.mv.Analyse(isb=False, delta=2, **analyse_args)
            if RefineMotion:
                bv2 = core.mv.Recalculate(Recalculate, bv2, **recalculate_args)
                fv2 = core.mv.Recalculate(Recalculate, fv2, **recalculate_args)
    else:
        super_render = super_search

    # Finally, MDegrain
    degrain_args = dict(thsad=thSAD, thsadc=thSADC, plane=plane, limit=limit, limitc=limitc, thscd1=thSCD1, thscd2=thSCD2)
    if not GlobalO:
        if interlaced:
            if tr >= 3:
                output = core.mv.Degrain3(mfilter, super_render, bv2, fv2, bv4, fv4, bv6, fv6, **degrain_args)
            elif tr == 2:
                output = core.mv.Degrain2(mfilter, super_render, bv2, fv2, bv4, fv4, **degrain_args)
            else:
                output = core.mv.Degrain1(mfilter, super_render, bv2, fv2, **degrain_args)
        else:
            if tr >= 3:
                output = core.mv.Degrain3(mfilter, super_render, bv1, fv1, bv2, fv2, bv3, fv3, **degrain_args)
            elif tr == 2:
                output = core.mv.Degrain2(mfilter, super_render, bv1, fv1, bv2, fv2, **degrain_args)
            else:
                output = core.mv.Degrain1(mfilter, super_render, bv1, fv1, **degrain_args)

    # Contrasharp (only sharpens luma)
    if not GlobalO and if0:
        if if1:
            if interlaced:
                CClip = CClip.std.SeparateFields(tff=tff)
        else:
            CClip = inputP

    # Output
    if not GlobalO:
        if if0:
            if interlaced:
                if ifC:
                    return Weave(ContraSharpening(output, CClip, planes=planes), tff=tff)
                else:
                    return Weave(LSFmod(output, strength=contrasharp, source=CClip, Lmode=0, soothe=False, defaults='slow'), tff=tff)
            elif ifC:
                return ContraSharpening(output, CClip, planes=planes)
            else:
                return LSFmod(output, strength=contrasharp, source=CClip, Lmode=0, soothe=False, defaults='slow')
        elif interlaced:
            return Weave(output, tff=tff)
        else:
            return output
    else:
        return input


# Dampen the grain just a little, to keep the original look
#
# Parameters:
#  limit (int)    - The spatial part won't change a pixel more than this. Default is 3
#  bias (int)     - The percentage of the spatial filter that will apply. Default is 24
#  RGmode (int)   - The spatial filter is RemoveGrain, this is its mode. Default is 4
#  tthr (int)     - Temporal threshold for fluxsmooth. Can be set "a good bit bigger" than usually. Default is 12
#  tlimit (int)   - The temporal filter won't change a pixel more than this. Default is 3
#  tbias (int)    - The percentage of the temporal filter that will apply. Default is 49
#  back (int)     - After all changes have been calculated, reduce all pixel changes by this value (shift "back" towards original value). Default is 1
#  planes (int[]) - Whether to process the corresponding plane. The other planes will be passed through unchanged.
def STPresso(clp, limit=3, bias=24, RGmode=4, tthr=12, tlimit=3, tbias=49, back=1, planes=None):
    if not isinstance(clp, vs.VideoNode):
        raise vs.Error('STPresso: This is not a clip')

    peak = (1 << clp.format.bits_per_sample) - 1

    if planes is None:
        planes = list(range(clp.format.num_planes))
    elif isinstance(planes, int):
        planes = [planes]

    limit = scale(limit, peak)
    tthr = scale(tthr, peak)
    tlimit = scale(tlimit, peak)
    back = scale(back, peak)

    LIM = cround(limit * 100 / bias - 1) if limit > 0 else scale(100 / bias, peak)
    TLIM = cround(tlimit * 100 / tbias - 1) if tlimit > 0 else scale(100 / tbias, peak)

    if limit < 0:
        expr = f'x y - abs {LIM} < x x 1 x y - dup abs / * - ?'
    else:
        expr = f'x y - abs {scale(1, peak)} < x x {LIM} + y < x {limit} + x {LIM} - y > x {limit} - x {100 - bias} * y {bias} * + 100 / ? ? ?'
    if tlimit < 0:
        texpr = f'x y - abs {TLIM} < x x 1 x y - dup abs / * - ?'
    else:
        texpr = f'x y - abs {scale(1, peak)} < x x {TLIM} + y < x {tlimit} + x {TLIM} - y > x {tlimit} - x {100 - tbias} * y {tbias} * + 100 / ? ? ?'

    if RGmode == 4:
        bzz = clp.std.Median(planes=planes)
    elif RGmode in [11, 12]:
        bzz = clp.std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1], planes=planes)
    elif RGmode == 19:
        bzz = clp.std.Convolution(matrix=[1, 1, 1, 1, 0, 1, 1, 1, 1], planes=planes)
    elif RGmode == 20:
        bzz = clp.std.Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1], planes=planes)
    else:
        bzz = clp.rgvs.RemoveGrain(mode=[RGmode if i in planes else 0 for i in range(clp.format.num_planes)])
    last = core.std.Expr([clp, bzz], expr=[expr if i in planes else '' for i in range(clp.format.num_planes)])
    if tthr > 0:
        last = core.std.Expr([last, core.std.MakeDiff(last, core.std.MakeDiff(bzz, bzz.flux.SmoothT(temporal_threshold=tthr, planes=planes), planes=planes), planes=planes)],
                             expr=[texpr if i in planes else '' for i in range(clp.format.num_planes)])
    if back > 0:
        expr = f'x {back} + y < x {back} + x {back} - y > x {back} - y ? ?'
        last = core.std.Expr([last, clp], expr=[expr if i in planes else '' for i in range(clp.format.num_planes)])
    return last


# a.k.a. BalanceBordersMod
def bbmod(c, cTop, cBottom, cLeft, cRight, thresh=128, blur=999):
    if not isinstance(c, vs.VideoNode):
        raise vs.Error('bbmod: This is not a clip')

    if c.format.color_family in [vs.GRAY, vs.RGB]:
        raise vs.Error('bbmod: Gray and RGB formats are not supported')

    if thresh <= 0:
        raise vs.Error('bbmod: thresh must be greater than 0')

    if blur <= 0:
        raise vs.Error('bbmod: blur must be greater than 0')

    neutral = 1 << (c.format.bits_per_sample - 1)
    peak = (1 << c.format.bits_per_sample) - 1

    BicubicResize = partial(core.resize.Bicubic, filter_param_a=1, filter_param_b=0)

    def btb(c, cTop):
        cWidth = c.width
        cHeight = c.height
        cTop = min(cTop, cHeight - 1)
        blurWidth = max(8, math.floor(cWidth / blur))

        c2 = c.resize.Point(cWidth * 2, cHeight * 2)

        last = c2.std.CropAbs(width=cWidth * 2, height=2, top=cTop * 2)
        last = last.resize.Point(cWidth * 2, cTop * 2)
        referenceBlurChroma = BicubicResize(BicubicResize(last.std.Expr(expr=[f'x {neutral} - abs 2 *', '']), blurWidth * 2, cTop * 2), cWidth * 2, cTop * 2)
        referenceBlur = BicubicResize(BicubicResize(last, blurWidth * 2, cTop * 2), cWidth * 2, cTop * 2)

        original = c2.std.CropAbs(width=cWidth * 2, height=cTop * 2)

        last = BicubicResize(original, blurWidth * 2, cTop * 2)
        originalBlurChroma = BicubicResize(BicubicResize(last.std.Expr(expr=[f'x {neutral} - abs 2 *', '']), blurWidth * 2, cTop * 2), cWidth * 2, cTop * 2)
        originalBlur = BicubicResize(BicubicResize(last, blurWidth * 2, cTop * 2), cWidth * 2, cTop * 2)

        balancedChroma = core.std.Expr([original, originalBlurChroma, referenceBlurChroma], expr=['', f'z y / 8 min 0.4 max x {neutral} - * {neutral} +'])
        expr = 'z {i} - y {i} - / 8 min 0.4 max x {i} - * {i} +'.format(i=scale(16, peak))
        balancedLuma = core.std.Expr([balancedChroma, originalBlur, referenceBlur], expr=[expr, 'z y - x +'])

        difference = core.std.MakeDiff(balancedLuma, original)
        difference = difference.std.Expr(expr=[f'x {scale(128 + thresh, peak)} min {scale(128 - thresh, peak)} max'])

        last = core.std.MergeDiff(original, difference)
        return core.std.StackVertical([last, c2.std.CropAbs(width=cWidth * 2, height=(cHeight - cTop) * 2, top=cTop * 2)]).resize.Point(cWidth, cHeight)

    if cTop > 0:
        c = btb(c, cTop)
    c = c.std.Transpose().std.FlipHorizontal()
    if cLeft > 0:
        c = btb(c, cLeft)
    c = c.std.Transpose().std.FlipHorizontal()
    if cBottom > 0:
        c = btb(c, cBottom)
    c = c.std.Transpose().std.FlipHorizontal()
    if cRight > 0:
        c = btb(c, cRight)
    return c.std.Transpose().std.FlipHorizontal()


# Apply the inverse sigmoid curve to a clip in linear luminance
def SigmoidInverse(src, thr=0.5, cont=6.5, planes=[0, 1, 2]):
    if not isinstance(src, vs.VideoNode) or src.format.bits_per_sample != 16:
        raise vs.Error('SigmoidInverse: This is not a 16-bit clip')

    if thr < 0 or thr > 1:
        raise vs.Error('SigmoidInverse: thr must be between 0.0 and 1.0 (inclusive)')

    if cont <= 0:
        raise vs.Error('SigmoidInverse: cont must be greater than 0.0')

    x0 = 1 / (1 + math.exp(cont * thr))
    x1m0 = 1 / (1 + math.exp(cont * (thr - 1))) - x0
    expr = f'{thr} 1 x 65536 / {x1m0} * {x0} + 0.000001 max / 1 - 0.000001 max log {cont} / - 65536 *'
    return src.std.Expr(expr=[expr if i in planes else '' for i in range(src.format.num_planes)])

# Convert back a clip to linear luminance
def SigmoidDirect(src, thr=0.5, cont=6.5, planes=[0, 1, 2]):
    if not isinstance(src, vs.VideoNode) or src.format.bits_per_sample != 16:
        raise vs.Error('SigmoidDirect: This is not a 16-bit clip')

    if thr < 0 or thr > 1:
        raise vs.Error('SigmoidDirect: thr must be between 0.0 and 1.0 (inclusive)')

    if cont <= 0:
        raise vs.Error('SigmoidDirect: cont must be greater than 0.0')

    x0 = 1 / (1 + math.exp(cont * thr))
    x1m0 = 1 / (1 + math.exp(cont * (thr - 1))) - x0
    expr = f'1 1 {cont} {thr} x 65536 / - * exp + / {x0} - {x1m0} / 65536 *'
    return src.std.Expr(expr=[expr if i in planes else '' for i in range(src.format.num_planes)])


# Parameters:
#  g1str (float)       - [0.0 - ???] strength of grain / for dark areas. Default is 7.0
#  g2str (float)       - [0.0 - ???] strength of grain / for midtone areas. Default is 5.0
#  g3str (float)       - [0.0 - ???] strength of grain / for bright areas. Default is 3.0
#  g1shrp (int)        - [0 - 100] sharpness of grain / for dark areas (NO EFFECT when g1size=1.0 !!). Default is 60
#  g2shrp (int)        - [0 - 100] sharpness of grain / for midtone areas (NO EFFECT when g2size=1.0 !!). Default is 66
#  g3shrp (int)        - [0 - 100] sharpness of grain / for bright areas (NO EFFECT when g3size=1.0 !!). Default is 80
#  g1size (float)      - [0.5 - 4.0] size of grain / for dark areas. Default is 1.5
#  g2size (float)      - [0.5 - 4.0] size of grain / for midtone areas. Default is 1.2
#  g3size (float)      - [0.5 - 4.0] size of grain / for bright areas. Default is 0.9
#  temp_avg (int)      - [0 - 100] percentage of noise's temporal averaging. Default is 0
#  ontop_grain (float) - [0 - ???] additional grain to put on top of prev. generated grain. Default is 0.0
#  th1 (int)           - start of dark->midtone mixing zone. Default is 24
#  th2 (int)           - end of dark->midtone mixing zone. Default is 56
#  th3 (int)           - start of midtone->bright mixing zone. Default is 128
#  th4 (int)           - end of midtone->bright mixing zone. Default is 160
def GrainFactory3(clp, g1str=7.0, g2str=5.0, g3str=3.0, g1shrp=60, g2shrp=66, g3shrp=80, g1size=1.5, g2size=1.2, g3size=0.9, temp_avg=0, ontop_grain=0.0, th1=24, th2=56, th3=128, th4=160):
    if not isinstance(clp, vs.VideoNode):
        raise vs.Error('GrainFactory3: This is not a clip')

    if clp.format.color_family == vs.RGB:
        raise vs.Error('GrainFactory3: RGB format is not supported')

    if clp.format.sample_type == vs.INTEGER:
        neutral = 1 << (clp.format.bits_per_sample - 1)
        peak = (1 << clp.format.bits_per_sample) - 1
    else:
        neutral = 0.0
        peak = 1.0

    if clp.format.color_family != vs.GRAY:
        clp_orig = clp
        clp = mvf.GetPlane(clp, 0)
    else:
        clp_orig = None

    ox = clp.width
    oy = clp.height
    sx1 = m4(ox / g1size)
    sy1 = m4(oy / g1size)
    sx1a = m4((ox + sx1) / 2)
    sy1a = m4((oy + sy1) / 2)
    sx2 = m4(ox / g2size)
    sy2 = m4(oy / g2size)
    sx2a = m4((ox + sx2) / 2)
    sy2a = m4((oy + sy2) / 2)
    sx3 = m4(ox / g3size)
    sy3 = m4(oy / g3size)
    sx3a = m4((ox + sx3) / 2)
    sy3a = m4((oy + sy3) / 2)

    b1 = g1shrp / -50 + 1
    b2 = g2shrp / -50 + 1
    b3 = g3shrp / -50 + 1
    b1a = b1 / 2
    b2a = b2 / 2
    b3a = b3 / 2
    c1 = (1 - b1) / 2
    c2 = (1 - b2) / 2
    c3 = (1 - b3) / 2
    c1a = (1 - b1a) / 2
    c2a = (1 - b2a) / 2
    c3a = (1 - b3a) / 2
    tmpavg = temp_avg / 100
    th1 = scale(th1, peak)
    th2 = scale(th2, peak)
    th3 = scale(th3, peak)
    th4 = scale(th4, peak)

    grainlayer1 = clp.std.BlankClip(width=sx1, height=sy1, color=[neutral]).grain.Add(var=g1str)
    if g1size != 1 and (sx1 != ox or sy1 != oy):
        if g1size > 1.5:
            grainlayer1 = grainlayer1.resize.Bicubic(sx1a, sy1a, filter_param_a=b1a, filter_param_b=c1a).resize.Bicubic(ox, oy, filter_param_a=b1a, filter_param_b=c1a)
        else:
            grainlayer1 = grainlayer1.resize.Bicubic(ox, oy, filter_param_a=b1, filter_param_b=c1)

    grainlayer2 = clp.std.BlankClip(width=sx2, height=sy2, color=[neutral]).grain.Add(var=g2str)
    if g2size != 1 and (sx2 != ox or sy2 != oy):
        if g2size > 1.5:
            grainlayer2 = grainlayer2.resize.Bicubic(sx2a, sy2a, filter_param_a=b2a, filter_param_b=c2a).resize.Bicubic(ox, oy, filter_param_a=b2a, filter_param_b=c2a)
        else:
            grainlayer2 = grainlayer2.resize.Bicubic(ox, oy, filter_param_a=b2, filter_param_b=c2)

    grainlayer3 = clp.std.BlankClip(width=sx3, height=sy3, color=[neutral]).grain.Add(var=g3str)
    if g3size != 1 and (sx3 != ox or sy3 != oy):
        if g3size > 1.5:
            grainlayer3 = grainlayer3.resize.Bicubic(sx3a, sy3a, filter_param_a=b3a, filter_param_b=c3a).resize.Bicubic(ox, oy, filter_param_a=b3a, filter_param_b=c3a)
        else:
            grainlayer3 = grainlayer3.resize.Bicubic(ox, oy, filter_param_a=b3, filter_param_b=c3)

    expr1 = f'x {th1} < 0 x {th2} > {peak} {peak} {th2 - th1} / x {th1} - * ? ?'
    expr2 = f'x {th3} < 0 x {th4} > {peak} {peak} {th4 - th3} / x {th3} - * ? ?'
    grainlayer = core.std.MaskedMerge(core.std.MaskedMerge(grainlayer1, grainlayer2, clp.std.Expr(expr=[expr1])), grainlayer3, clp.std.Expr(expr=[expr2]))

    if temp_avg > 0:
        grainlayer = core.std.Merge(grainlayer, AverageFrames(grainlayer, weights=[1] * 3), weight=[tmpavg])
    if ontop_grain > 0:
        grainlayer = grainlayer.grain.Add(var=ontop_grain)

    result = core.std.MakeDiff(clp, grainlayer)

    if clp_orig is not None:
        result = core.std.ShufflePlanes([result, clp_orig], planes=[0, 1, 2], colorfamily=clp_orig.format.color_family)
    return result


#------------------------------------------------------------------------------#
#                                                                              #
#                         InterFrame 2.8.2 by SubJunk                          #
#                                                                              #
#         A frame interpolation script that makes accurate estimations         #
#                   about the content of non-existent frames                   #
#      Its main use is to give videos higher framerates like newer TVs do      #
#------------------------------------------------------------------------------#
def InterFrame(Input, Preset='Medium', Tuning='Film', NewNum=None, NewDen=1, GPU=False, InputType='2D', OverrideAlgo=None, OverrideArea=None, FrameDouble=False):
    if not isinstance(Input, vs.VideoNode):
        raise vs.Error('InterFrame: This is not a clip')

    # Validate inputs
    Preset = Preset.lower()
    Tuning = Tuning.lower()
    InputType = InputType.upper()

    if Preset not in ['medium', 'fast', 'faster', 'fastest']:
        raise vs.Error(f"InterFrame: '{Preset}' is not a valid preset")

    if Tuning not in ['film', 'smooth', 'animation', 'weak']:
        raise vs.Error(f"InterFrame: '{Tuning}' is not a valid tuning")

    if InputType not in ['2D', 'SBS', 'OU', 'HSBS', 'HOU']:
        raise vs.Error(f"InterFrame: '{InputType}' is not a valid InputType")

    def InterFrameProcess(clip):
        # Create SuperString
        if Preset in ['fast', 'faster', 'fastest']:
            SuperString = '{pel:1,'
        else:
            SuperString = '{'

        SuperString += 'gpu:1}' if GPU else 'gpu:0}'

        # Create VectorsString
        if Tuning == 'animation' or Preset == 'fastest':
            VectorsString = '{block:{w:32,'
        elif Preset in ['fast', 'faster'] or not GPU:
            VectorsString = '{block:{w:16,'
        else:
            VectorsString = '{block:{w:8,'

        if Tuning == 'animation' or Preset == 'fastest':
            VectorsString += 'overlap:0'
        elif Preset == 'faster' and GPU:
            VectorsString += 'overlap:1'
        else:
            VectorsString += 'overlap:2'

        if Tuning == 'animation':
            VectorsString += '},main:{search:{coarse:{type:2,'
        elif Preset == 'faster':
            VectorsString += '},main:{search:{coarse:{'
        else:
            VectorsString += '},main:{search:{distance:0,coarse:{'

        if Tuning == 'animation':
            VectorsString += 'distance:-6,satd:false},distance:0,'
        elif Tuning == 'weak':
            VectorsString += 'distance:-1,trymany:true,'
        else:
            VectorsString += 'distance:-10,'

        if Tuning == 'animation' or Preset in ['faster', 'fastest']:
            VectorsString += 'bad:{sad:2000}}}}}'
        elif Tuning == 'weak':
            VectorsString += 'bad:{sad:2000}}}},refine:[{thsad:250,search:{distance:-1,satd:true}}]}'
        else:
            VectorsString += 'bad:{sad:2000}}}},refine:[{thsad:250}]}'

        # Create SmoothString
        if NewNum is not None:
            SmoothString = '{rate:{num:' + repr(NewNum) + ',den:' + repr(NewDen) + ',abs:true},'
        elif clip.fps_num / clip.fps_den in [15, 25, 30] or FrameDouble:
            SmoothString = '{rate:{num:2,den:1,abs:false},'
        else:
            SmoothString = '{rate:{num:60000,den:1001,abs:true},'

        if OverrideAlgo is not None:
            SmoothString += 'algo:' + repr(OverrideAlgo) + ',mask:{cover:80,'
        elif Tuning == 'animation':
            SmoothString += 'algo:2,mask:{'
        elif Tuning == 'smooth':
            SmoothString += 'algo:23,mask:{'
        else:
            SmoothString += 'algo:13,mask:{cover:80,'

        if OverrideArea is not None:
            SmoothString += f'area:{OverrideArea}'
        elif Tuning == 'smooth':
            SmoothString += 'area:150'
        else:
            SmoothString += 'area:0'

        if Tuning == 'weak':
            SmoothString += ',area_sharp:1.2},scene:{blend:true,mode:0,limits:{blocks:50}}}'
        else:
            SmoothString += ',area_sharp:1.2},scene:{blend:true,mode:0}}'

        # Make interpolation vector clip
        Super = clip.svp1.Super(SuperString)
        Vectors = core.svp1.Analyse(Super['clip'], Super['data'], clip, VectorsString)

        # Put it together
        return core.svp2.SmoothFps(clip, Super['clip'], Super['data'], Vectors['clip'], Vectors['data'], SmoothString)

    # Get either 1 or 2 clips depending on InputType
    if InputType == 'SBS':
        FirstEye = InterFrameProcess(Input.std.Crop(right=Input.width // 2))
        SecondEye = InterFrameProcess(Input.std.Crop(left=Input.width // 2))
        return core.std.StackHorizontal([FirstEye, SecondEye])
    elif InputType == 'OU':
        FirstEye = InterFrameProcess(Input.std.Crop(bottom=Input.height // 2))
        SecondEye = InterFrameProcess(Input.std.Crop(top=Input.height // 2))
        return core.std.StackVertical([FirstEye, SecondEye])
    elif InputType == 'HSBS':
        FirstEye = InterFrameProcess(Input.std.Crop(right=Input.width // 2).resize.Spline36(Input.width, Input.height))
        SecondEye = InterFrameProcess(Input.std.Crop(left=Input.width // 2).resize.Spline36(Input.width, Input.height))
        return core.std.StackHorizontal([FirstEye.resize.Spline36(Input.width // 2, Input.height), SecondEye.resize.Spline36(Input.width // 2, Input.height)])
    elif InputType == 'HOU':
        FirstEye = InterFrameProcess(Input.std.Crop(bottom=Input.height // 2).resize.Spline36(Input.width, Input.height))
        SecondEye = InterFrameProcess(Input.std.Crop(top=Input.height // 2).resize.Spline36(Input.width, Input.height))
        return core.std.StackVertical([FirstEye.resize.Spline36(Input.width, Input.height // 2), SecondEye.resize.Spline36(Input.width, Input.height // 2)])
    else:
        return InterFrameProcess(Input)


# column is the column you want to work on.
def FixColumnBrightness(c, column, input_low, input_high, output_low, output_high):
    if not isinstance(c, vs.VideoNode):
        raise vs.Error('FixColumnBrightness: This is not a clip')

    if c.format.color_family == vs.RGB:
        raise vs.Error('FixColumnBrightness: RGB format is not supported')

    peak = (1 << c.format.bits_per_sample) - 1

    if c.format.color_family != vs.GRAY:
        c_orig = c
        c = mvf.GetPlane(c, 0)
    else:
        c_orig = None

    input_low = scale(input_low, peak)
    input_high = scale(input_high, peak)
    output_low = scale(output_low, peak)
    output_high = scale(output_high, peak)

    last = SmoothLevels(c, input_low, 1, input_high, output_low, output_high, Smode=0)
    last = last.std.CropAbs(width=1, height=c.height, left=column)
    last = Overlay(c, last, x=column)
    if c_orig is not None:
        last = core.std.ShufflePlanes([last, c_orig], planes=[0, 1, 2], colorfamily=c_orig.format.color_family)
    return last


# row is the row you want to work on.
def FixRowBrightness(c, row, input_low, input_high, output_low, output_high):
    if not isinstance(c, vs.VideoNode):
        raise vs.Error('FixRowBrightness: This is not a clip')

    if c.format.color_family == vs.RGB:
        raise vs.Error('FixRowBrightness: RGB format is not supported')

    peak = (1 << c.format.bits_per_sample) - 1

    if c.format.color_family != vs.GRAY:
        c_orig = c
        c = mvf.GetPlane(c, 0)
    else:
        c_orig = None

    input_low = scale(input_low, peak)
    input_high = scale(input_high, peak)
    output_low = scale(output_low, peak)
    output_high = scale(output_high, peak)

    last = SmoothLevels(c, input_low, 1, input_high, output_low, output_high, Smode=0)
    last = last.std.CropAbs(width=c.width, height=1, top=row)
    last = Overlay(c, last, y=row)
    if c_orig is not None:
        last = core.std.ShufflePlanes([last, c_orig], planes=[0, 1, 2], colorfamily=c_orig.format.color_family)
    return last


# protect_value determines which pixels wouldn't be affected by the filter. Increasing the value, you protect the pixels with lower luma.
def FixColumnBrightnessProtect(c, column, input_low, input_high, output_low, output_high, protect_value=20):
    if not isinstance(c, vs.VideoNode):
        raise vs.Error('FixColumnBrightnessProtect: This is not a clip')

    if c.format.color_family == vs.RGB:
        raise vs.Error('FixColumnBrightnessProtect: RGB format is not supported')

    peak = (1 << c.format.bits_per_sample) - 1

    if c.format.color_family != vs.GRAY:
        c_orig = c
        c = mvf.GetPlane(c, 0)
    else:
        c_orig = None

    input_low = scale(255 - input_low, peak)
    input_high = scale(255 - input_high, peak)
    output_low = scale(255 - output_low, peak)
    output_high = scale(255 - output_high, peak)
    protect_value = scale(protect_value, peak)

    last = SmoothLevels(c.std.Invert(), input_low, 1, input_high, output_low, output_high, protect=protect_value, Smode=0).std.Invert()
    last = last.std.CropAbs(width=1, height=c.height, left=column)
    last = Overlay(c, last, x=column)
    if c_orig is not None:
        last = core.std.ShufflePlanes([last, c_orig], planes=[0, 1, 2], colorfamily=c_orig.format.color_family)
    return last


def FixRowBrightnessProtect(c, row, input_low, input_high, output_low, output_high, protect_value=20):
    if not isinstance(c, vs.VideoNode):
        raise vs.Error('FixRowBrightnessProtect: This is not a clip')

    if c.format.color_family == vs.RGB:
        raise vs.Error('FixRowBrightnessProtect: RGB format is not supported')

    shift = c.format.bits_per_sample - 8
    peak = (1 << c.format.bits_per_sample) - 1

    if c.format.color_family != vs.GRAY:
        c_orig = c
        c = mvf.GetPlane(c, 0)
    else:
        c_orig = None

    input_low = scale(255 - input_low, peak)
    input_high = scale(255 - input_high, peak)
    output_low = scale(255 - output_low, peak)
    output_high = scale(255 - output_high, peak)
    protect_value = scale(protect_value, peak)

    last = SmoothLevels(c.std.Invert(), input_low, 1, input_high, output_low, output_high, protect=protect_value, Smode=0).std.Invert()
    last = last.std.CropAbs(width=c.width, height=1, top=row)
    last = Overlay(c, last, y=row)
    if c_orig is not None:
        last = core.std.ShufflePlanes([last, c_orig], planes=[0, 1, 2], colorfamily=c_orig.format.color_family)
    return last


# adj_val should be a number x where -100 < x < 100. This parameter decides
# how much the brightness should be affected. Numbers below 0 will make it darker
# and number above 0 will make it brighter.
#
# prot_val is the protect value. This is what makes it behave differently than the
# normal FixBrightness. Any luma above (255-prot_val) will not be affected which is
# the basic idea of the protect script.
def FixColumnBrightnessProtect2(c, column, adj_val, prot_val=16):
    if not isinstance(c, vs.VideoNode):
        raise vs.Error('FixColumnBrightnessProtect2: This is not a clip')

    if c.format.color_family == vs.RGB:
        raise vs.Error('FixColumnBrightnessProtect2: RGB format is not supported')

    if not (-100 < adj_val < 100):
        raise vs.Error('FixColumnBrightnessProtect2: adj_val must be greater than -100 and less than 100')

    peak = (1 << c.format.bits_per_sample) - 1

    if c.format.color_family != vs.GRAY:
        c_orig = c
        c = mvf.GetPlane(c, 0)
    else:
        c_orig = None

    expr = f'x {scale(16, peak)} - {100 - adj_val} / 100 * {scale(16, peak)} + x {scale(255 - prot_val, peak)} - -10 / 0 max 1 min * x x {scale(245 - prot_val, peak)} - 10 / 0 max 1 min * +'
    last = c.std.Expr(expr=[expr])
    last = last.std.CropAbs(width=1, height=c.height, left=column)
    last = Overlay(c, last, x=column)
    if c_orig is not None:
        last = core.std.ShufflePlanes([last, c_orig], planes=[0, 1, 2], colorfamily=c_orig.format.color_family)
    return last


def FixRowBrightnessProtect2(c, row, adj_val, prot_val=16):
    if not isinstance(c, vs.VideoNode):
        raise vs.Error('FixRowBrightnessProtect2: This is not a clip')

    if c.format.color_family == vs.RGB:
        raise vs.Error('FixRowBrightnessProtect2: RGB format is not supported')

    if not (-100 < adj_val < 100):
        raise vs.Error('FixRowBrightnessProtect2: adj_val must be greater than -100 and less than 100')

    peak = (1 << c.format.bits_per_sample) - 1

    if c.format.color_family != vs.GRAY:
        c_orig = c
        c = mvf.GetPlane(c, 0)
    else:
        c_orig = None

    expr = f'x {scale(16, peak)} - {100 - adj_val} / 100 * {scale(16, peak)} + x {scale(255 - prot_val, peak)} - -10 / 0 max 1 min * x x {scale(245 - prot_val, peak)} - 10 / 0 max 1 min * +'
    last = c.std.Expr(expr=[expr])
    last = last.std.CropAbs(width=c.width, height=1, top=row)
    last = Overlay(c, last, y=row)
    if c_orig is not None:
        last = core.std.ShufflePlanes([last, c_orig], planes=[0, 1, 2], colorfamily=c_orig.format.color_family)
    return last


#########################################################################################
###                                                                                   ###
###                      function Smooth Levels : SmoothLevels()                      ###
###                                                                                   ###
###                                v1.02 by "LaTo INV."                               ###
###                                                                                   ###
###                                  28 January 2009                                  ###
###                                                                                   ###
#########################################################################################
###
###
### /!\ Needed filters : RGVS, f3kdb
### --------------------
###
###
###
### +---------+
### | GENERAL |
### +---------+
###
### Levels options:
### ---------------
### input_low, gamma, input_high, output_low, output_high [default: 0, 1.0, maximum value of input format, 0, maximum value of input format]
### /!\ The value is not internally normalized on an 8-bit scale, and must be scaled to the bit depth of input format manually by users
###
### chroma [default: 50]
### ---------------------
### 0   = no chroma processing     (similar as Ylevels)
### xx  = intermediary
### 100 = normal chroma processing (similar as Levels)
###
### limiter [default: 0]
### --------------------
### 0 = no limiter             (similar as Ylevels)
### 1 = input limiter
### 2 = output limiter         (similar as Levels: coring=false)
### 3 = input & output limiter (similar as Levels: coring=true)
###
###
###
### +----------+
### | LIMITING |
### +----------+
###
### Lmode [default: 0]
### ------------------
### 0 = no limit
### 1 = limit conversion on dark & bright areas (apply conversion @0%   at luma=0 & @100% at luma=Ecenter & @0% at luma=255)
### 2 = limit conversion on dark areas          (apply conversion @0%   at luma=0 & @100% at luma=255)
### 3 = limit conversion on bright areas        (apply conversion @100% at luma=0 & @0%   at luma=255)
###
### DarkSTR [default: 100]
### ----------------------
### Strength for limiting: the higher, the more conversion are reduced on dark areas (for Lmode=1&2)
###
### BrightSTR [default: 100]
### ------------------------
### Strength for limiting: the higher, the more conversion are reduced on bright areas (for Lmode=1&3)
###
### Ecenter [default: median value of input format]
### ----------------------
### Center of expression for Lmode=1
### /!\ The value is not internally normalized on an 8-bit scale, and must be scaled to the bit depth of input format manually by users
###
### protect [default: -1]
### ---------------------
### -1  = protect off
### >=0 = pure black protection
###       ---> don't apply conversion on pixels egal or below this value
###            (ex: with 16, the black areas like borders and generic are untouched so they don't look washed out)
### /!\ The value is not internally normalized on an 8-bit scale, and must be scaled to the bit depth of input format manually by users
###
### Ecurve [default: 1]
### -------------------
### Curve used for limit & protect:
### 0 = use sine curve
### 1 = use linear curve
###
###
###
### +-----------+
### | SMOOTHING |
### +-----------+
###
### Smode [default: -2]
### -------------------
### 2  = smooth on, maxdiff must be < to "255/Mfactor"
### 1  = smooth on, maxdiff must be < to "128/Mfactor"
### 0  = smooth off
### -1 = smooth on if maxdiff < "128/Mfactor", else off
### -2 = smooth on if maxdiff < "255/Mfactor", else off
###
### Mfactor [default: 2]
### --------------------
### The higher, the more precise but the less maxdiff alowed:
### maxdiff=128/Mfactor for Smode1&-1 and maxdiff=255/Mfactor for Smode2&-2
###
### RGmode [default: 12]
### --------------------
### In strength order: + 19 > 12 >> 20 > 11 -
###
### useDB [default: false]
### ---------------------
### Use f3kdb on top of removegrain: prevent posterize when doing levels conversion
###
###
#########################################################################################
def SmoothLevels(input, input_low=0, gamma=1.0, input_high=None, output_low=0, output_high=None, chroma=50, limiter=0, Lmode=0, DarkSTR=100, BrightSTR=100, Ecenter=None, protect=-1, Ecurve=1,
                 Smode=-2, Mfactor=2, RGmode=12, useDB=False):
    if not isinstance(input, vs.VideoNode):
        raise vs.Error('SmoothLevels: This is not a clip')

    if input.format.color_family == vs.RGB:
        raise vs.Error('SmoothLevels: RGB format is not supported')

    isGray = (input.format.color_family == vs.GRAY)

    if input.format.sample_type == vs.INTEGER:
        neutral = [1 << (input.format.bits_per_sample - 1)] * 2
        peak = (1 << input.format.bits_per_sample) - 1
    else:
        neutral = [0.5, 0.0]
        peak = 1.0

    if chroma <= 0 and not isGray:
        input_orig = input
        input = mvf.GetPlane(input, 0)
    else:
        input_orig = None

    if input_high is None:
        input_high = peak

    if output_high is None:
        output_high = peak

    if Ecenter is None:
        Ecenter = neutral[0]

    if gamma <= 0:
        raise vs.Error('SmoothLevels: gamma must be greater than 0.0')

    if Ecenter <= 0 or Ecenter >= peak:
        raise vs.Error('SmoothLevels: Ecenter must be greater than 0 and less than maximum value of input format')

    if Mfactor <= 0:
        raise vs.Error('SmoothLevels: Mfactor must be greater than 0')

    if RGmode == 4:
        RemoveGrain = partial(core.std.Median)
    elif RGmode in [11, 12]:
        RemoveGrain = partial(core.std.Convolution, matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])
    elif RGmode == 19:
        RemoveGrain = partial(core.std.Convolution, matrix=[1, 1, 1, 1, 0, 1, 1, 1, 1])
    elif RGmode == 20:
        RemoveGrain = partial(core.std.Convolution, matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1])
    else:
        RemoveGrain = partial(core.rgvs.RemoveGrain, mode=[RGmode])

    ### EXPRESSION
    exprY = f'x {input_low} - {input_high - input_low + (input_high == input_low)} / {1 / gamma} pow {output_high - output_low} * {output_low} +'

    if chroma > 0 and not isGray:
        scaleC = ((output_high - output_low) / (input_high - input_low + (input_high == input_low)) + 100 / chroma - 1) / (100 / chroma)
        exprC = f'x {neutral[1]} - {scaleC} * {neutral[1]} +'

    Dstr = DarkSTR / 100
    Bstr = BrightSTR / 100

    if Lmode <= 0:
        exprL = '1'
    elif Ecurve <= 0:
        raise vs.Error('SmoothLevels: Ecurve=0 is unusable at the moment due to missing sin operator in Expr')
        if Lmode == 1:
            exprL = f'x {Ecenter} < x {333 / 106} * {2 * Ecenter} / sin {Dstr} pow x {Ecenter} > {(333 / 106) / 2} x {Ecenter} - {333 / 106} * {2 * (peak - Ecenter)} / + sin {Bstr} pow 1 ? ?'
        elif Lmode == 2:
            exprL = f'x {333 / 106} * {2 * peak} / sin {Dstr} pow'
        else:
            exprL = f'{(333 / 106) / 2} x {333 / 106} * {2 * peak} / + sin {Bstr} pow'
    else:
        if Lmode == 1:
            exprL = f'x {Ecenter} < x {Ecenter} / abs {Dstr} pow x {Ecenter} > 1 x {Ecenter} - {peak - Ecenter} / abs - {Bstr} pow 1 ? ?'
        elif Lmode == 2:
            exprL = f'1 x {peak} - {peak} / abs - {Dstr} pow'
        else:
            exprL = f'x {peak} - {peak} / abs {Bstr} pow'

    if protect <= -1:
        exprP = '1'
    elif Ecurve <= 0:
        raise vs.Error('SmoothLevels: Ecurve=0 is unusable at the moment due to missing sin operator in Expr')
        exprP = f'x {protect} <= 0 x {protect + scale(16, peak)} >= 1 x {protect} - {333 / 106} * {2 * scale(16, peak)} / sin ? ?'
    else:
        exprP = f'x {protect} <= 0 x {protect + scale(16, peak)} >= 1 x {protect} - {scale(16, peak)} / abs ? ?'

    ### PROCESS
    if limiter == 1 or limiter >= 3:
        limitI = input.std.Expr(expr=[f'x {input_low} max {input_high} min'])
    else:
        limitI = input

    expr = exprL + ' ' + exprP + ' * ' + exprY + ' x - * x +'
    level = limitI.std.Expr(expr=[expr] if chroma <= 0 or isGray else [expr, exprC])
    diff = core.std.Expr([limitI, level], expr=[f'x y - {Mfactor} * {neutral[1]} +'])
    process = RemoveGrain(diff)
    if useDB:
        process = process.std.Expr(expr=[f'x {neutral[1]} - {Mfactor} / {neutral[1]} +']).f3kdb.Deband(grainy=0, grainc=0, output_depth=input.format.bits_per_sample)
        smth = core.std.MakeDiff(limitI, process)
    else:
        smth = core.std.Expr([limitI, process], expr=[f'x y {neutral[1]} - {Mfactor} / -'])

    level2 = core.std.Expr([limitI, diff], expr=[f'x y {neutral[1]} - {Mfactor} / -'])
    diff2 = core.std.Expr([level2, level], expr=[f'x y - {Mfactor} * {neutral[1]} +'])
    process2 = RemoveGrain(diff2)
    if useDB:
        process2 = process2.std.Expr(expr=[f'x {neutral[1]} - {Mfactor} / {neutral[1]} +']).f3kdb.Deband(grainy=0, grainc=0, output_depth=input.format.bits_per_sample)
        smth2 = core.std.MakeDiff(smth, process2)
    else:
        smth2 = core.std.Expr([smth, process2], expr=[f'x y {neutral[1]} - {Mfactor} / -'])

    mask1 = core.std.Expr([limitI, level], expr=[f'x y - abs {neutral[0] / Mfactor} >= {peak} 0 ?'])
    mask2 = core.std.Expr([limitI, level], expr=[f'x y - abs {peak / Mfactor} >= {peak} 0 ?'])

    if Smode >= 2:
        Slevel = smth2
    elif Smode == 1:
        Slevel = smth
    elif Smode == -1:
        Slevel = core.std.MaskedMerge(smth, level, mask1)
    elif Smode <= -2:
        Slevel = core.std.MaskedMerge(core.std.MaskedMerge(smth, smth2, mask1), level, mask2)
    else:
        Slevel = level

    if limiter >= 2:
        limitO = Slevel.std.Expr(expr=[f'x {output_low} max {output_high} min'])
    else:
        limitO = Slevel

    if input_orig is not None:
        limitO = core.std.ShufflePlanes([limitO, input_orig], planes=[0, 1, 2], colorfamily=input_orig.format.color_family)
    return limitO


##############################
# FastLineDarken 1.4x MT MOD #
##############################
#
# Written by Vectrangle    (http://forum.doom9.org/showthread.php?t=82125)
# Didée: - Speed Boost, Updated: 11th May 2007
# Dogway - added protection option. 12-May-2011
#
# Parameters are:
#  strength (integer)   - Line darkening amount, 0-256. Default 48. Represents the _maximum_ amount
#                         that the luma will be reduced by, weaker lines will be reduced by
#                         proportionately less.
#  protection (integer) - Prevents the darkest lines from being darkened. Protection acts as a threshold.
#                         Values range from 0 (no prot) to ~50 (protect everything)
#  luma_cap (integer)   - value from 0 (black) to 255 (white), used to stop the darkening
#                         determination from being 'blinded' by bright pixels, and to stop grey
#                         lines on white backgrounds being darkened. Any pixels brighter than
#                         luma_cap are treated as only being as bright as luma_cap. Lowering
#                         luma_cap tends to reduce line darkening. 255 disables capping. Default 191.
#  threshold (integer)  - any pixels that were going to be darkened by an amount less than
#                         threshold will not be touched. setting this to 0 will disable it, setting
#                         it to 4 (default) is recommended, since often a lot of random pixels are
#                         marked for very slight darkening and a threshold of about 4 should fix
#                         them. Note if you set threshold too high, some lines will not be darkened
#  thinning (integer)   - optional line thinning amount, 0-256. Setting this to 0 will disable it,
#                         which is gives a _big_ speed increase. Note that thinning the lines will
#                         inherently darken the remaining pixels in each line a little. Default 0.
def FastLineDarkenMOD(c, strength=48, protection=5, luma_cap=191, threshold=4, thinning=0):
    if not isinstance(c, vs.VideoNode):
        raise vs.Error('FastLineDarkenMOD: This is not a clip')

    if c.format.color_family == vs.RGB:
        raise vs.Error('FastLineDarkenMOD: RGB format is not supported')

    peak = (1 << c.format.bits_per_sample) - 1 if c.format.sample_type == vs.INTEGER else 1.0

    if c.format.color_family != vs.GRAY:
        c_orig = c
        c = mvf.GetPlane(c, 0)
    else:
        c_orig = None

    ## parameters ##
    Str = strength / 128
    lum = scale(luma_cap, peak)
    thr = scale(threshold, peak)
    thn = thinning / 16

    ## filtering ##
    exin = c.std.Maximum(threshold=peak / (protection + 1)).std.Minimum()
    thick = core.std.Expr([c, exin], expr=[f'y {lum} < y {lum} ? x {thr} + > x y {lum} < y {lum} ? - 0 ? {Str} * x +'])
    if thinning <= 0:
        last = thick
    else:
        diff = core.std.Expr([c, exin], expr=[f'y {lum} < y {lum} ? x {thr} + > x y {lum} < y {lum} ? - 0 ? {scale(127, peak)} +'])
        linemask = diff.std.Minimum().std.Expr(expr=[f'x {scale(127, peak)} - {thn} * {peak} +']).std.Convolution(matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1])
        thin = core.std.Expr([c.std.Maximum(), diff], expr=[f'x y {scale(127, peak)} - {Str} 1 + * +'])
        last = core.std.MaskedMerge(thin, thick, linemask)

    if c_orig is not None:
        last = core.std.ShufflePlanes([last, c_orig], planes=[0, 1, 2], colorfamily=c_orig.format.color_family)
    return last


#####################
## Toon v0.82 edit ##
#####################
#
# function created by mf
#   support by Soulhunter ;-)
#   ported to masktools v2 and optimized by Didee (0.82)
#   added parameters and smaller changes by MOmonster (0.82 edited)
#
# toon v0.8 is the newest light-weight build of mf´s nice line darken function mf_toon
#
# Parameters:
#  str (float) - Strength of the line darken. Default is 1.0
#  l_thr (int) - Lower threshold for the linemask. Default is 2
#  u_thr (int) - Upper threshold for the linemask. Default is 12
#  blur (int)  - "blur" parameter of AWarpSharp2. Default is 2
#  depth (int) - "depth" parameter of AWarpSharp2. Default is 32
def Toon(input, str=1.0, l_thr=2, u_thr=12, blur=2, depth=32):
    if not isinstance(input, vs.VideoNode):
        raise vs.Error('Toon: This is not a clip')

    if input.format.color_family == vs.RGB:
        raise vs.Error('Toon: RGB format is not supported')

    neutral = 1 << (input.format.bits_per_sample - 1)
    peak = (1 << input.format.bits_per_sample) - 1
    multiple = peak / 255

    if input.format.color_family != vs.GRAY:
        input_orig = input
        input = mvf.GetPlane(input, 0)
    else:
        input_orig = None

    lthr = neutral + scale(l_thr, peak)
    lthr8 = lthr / multiple
    uthr = neutral + scale(u_thr, peak)
    uthr8 = uthr / multiple
    ludiff = u_thr - l_thr

    last = core.std.MakeDiff(input.std.Maximum().std.Minimum(), input)
    last = core.std.Expr([last, Padding(last, 6, 6, 6, 6).warp.AWarpSharp2(blur=blur, depth=depth).std.Crop(6, 6, 6, 6)], expr=['x y min'])
    expr = f'y {lthr} <= {neutral} y {uthr} >= x {uthr8} y {multiple} / - 128 * x {multiple} / y {multiple} / {lthr8} - * + {ludiff} / {multiple} * ? {neutral} - {str} * {neutral} + ?'
    last = core.std.MakeDiff(input, core.std.Expr([last, last.std.Maximum()], expr=[expr]))

    if input_orig is not None:
        last = core.std.ShufflePlanes([last, input_orig], planes=[0, 1, 2], colorfamily=input_orig.format.color_family)
    return last


################################################################################################
###                                                                                          ###
###                       LimitedSharpenFaster MOD : function LSFmod()                       ###
###                                                                                          ###
###                                Modded Version by LaTo INV.                               ###
###                                                                                          ###
###                                  v1.9 - 05 October 2009                                  ###
###                                                                                          ###
################################################################################################
###
### +--------------+
### | DEPENDENCIES |
### +--------------+
###
### -> RGVS
### -> CAS
###
###
###
### +---------+
### | GENERAL |
### +---------+
###
### strength [int]
### --------------
### Strength of the sharpening
###
### Smode [int: 1,2,3]
### ----------------------
### Sharpen mode:
###    =1 : Range sharpening
###    =2 : Nonlinear sharpening (corrected version)
###    =3 : Contrast Adaptive Sharpening
###
### Smethod [int: 1,2,3]
### --------------------
### Sharpen method: (only used in Smode=1,2)
###    =1 : 3x3 kernel
###    =2 : Min/Max
###    =3 : Min/Max + 3x3 kernel
###
### kernel [int: 11,12,19,20]
### -------------------------
### Kernel used in Smethod=1&3
### In strength order: + 19 > 12 >> 20 > 11 -
###
###
###
### +---------+
### | SPECIAL |
### +---------+
###
### preblur [int: 0,1,2,3]
### --------------------------------
### Mode to avoid noise sharpening & ringing:
###    =-1 : No preblur
###    = 0 : MinBlur(0)
###    = 1 : MinBlur(1)
###    = 2 : MinBlur(2)
###    = 3 : DFTTest
###
### secure [bool]
### -------------
### Mode to avoid banding & oil painting (or face wax) effect of sharpening
###
### source [clip]
### -------------
### If source is defined, LSFmod doesn't sharp more a denoised clip than this source clip
### In this mode, you can safely set Lmode=0 & PP=off
###    Usage:   denoised.LSFmod(source=source)
###    Example: last.FFT3DFilter().LSFmod(source=last,Lmode=0,soft=0)
###
###
###
### +----------------------+
### | NONLINEAR SHARPENING |
### +----------------------+
###
### Szrp [int]
### ----------
### Zero Point:
###    - differences below Szrp are amplified (overdrive sharpening)
###    - differences above Szrp are reduced   (reduced sharpening)
###
### Spwr [int]
### ----------
### Power: exponent for sharpener
###
### SdmpLo [int]
### ------------
### Damp Low: reduce sharpening for small changes [0:disable]
###
### SdmpHi [int]
### ------------
### Damp High: reduce sharpening for big changes [0:disable]
###
###
###
### +----------+
### | LIMITING |
### +----------+
###
### Lmode [int: ...,0,1,2,3,4]
### --------------------------
### Limit mode:
###    <0 : Limit with repair (ex: Lmode=-1 --> repair(1), Lmode=-5 --> repair(5)...)
###    =0 : No limit
###    =1 : Limit to over/undershoot
###    =2 : Limit to over/undershoot on edges and no limit on not-edges
###    =3 : Limit to zero on edges and to over/undershoot on not-edges
###    =4 : Limit to over/undershoot on edges and to over/undershoot2 on not-edges
###
### overshoot [int]
### ---------------
### Limit for pixels that get brighter during sharpening
###
### undershoot [int]
### ----------------
### Limit for pixels that get darker during sharpening
###
### overshoot2 [int]
### ----------------
### Same as overshoot, only for Lmode=4
###
### undershoot2 [int]
### -----------------
### Same as undershoot, only for Lmode=4
###
###
###
### +-----------------+
### | POST-PROCESSING |
### +-----------------+
###
### soft [int: -2,-1,0...100]
### -------------------------
### Soft the sharpening effect (-1 = old autocalculate, -2 = new autocalculate)
###
### soothe [bool]
### -------------
###    =True  : Enable soothe temporal stabilization
###    =False : Disable soothe temporal stabilization
###
### keep [int: 0...100]
### -------------------
### Minimum percent of the original sharpening to keep (only with soothe=True)
###
###
###
### +-------+
### | EDGES |
### +-------+
###
### edgemode [int: -1,0,1,2]
### ------------------------
###    =-1 : Show edgemask
###    = 0 : Sharpening all
###    = 1 : Sharpening only edges
###    = 2 : Sharpening only not-edges
###
### edgemaskHQ [bool]
### -----------------
###    =True  : Original edgemask
###    =False : Faster edgemask
###
###
###
### +------------+
### | UPSAMPLING |
### +------------+
###
### ss_x ; ss_y [float]
### -------------------
### Supersampling factor (reduce aliasing on edges)
###
### dest_x ; dest_y [int]
### ---------------------
### Output resolution after sharpening (avoid a resizing step)
###
###
###
### +----------+
### | SETTINGS |
### +----------+
###
### defaults [string: "old" or "slow" or "fast"]
### --------------------------------------------
###    = "old"  : Reset settings to original version (output will be THE SAME AS LSF)
###    = "slow" : Enable SLOW modded version settings
###    = "fast" : Enable FAST modded version settings
###  --> /!\ [default:"fast"]
###
###
### defaults="old" :  - strength    = 100
### ----------------  - Smode       = 1
###                   - Smethod     = Smode==1?2:1
###                   - kernel      = 11
###
###                   - preblur     = -1
###                   - secure      = false
###                   - source      = undefined
###
###                   - Szrp        = 16
###                   - Spwr        = 2
###                   - SdmpLo      = strength/25
###                   - SdmpHi      = 0
###
###                   - Lmode       = 1
###                   - overshoot   = 1
###                   - undershoot  = overshoot
###                   - overshoot2  = overshoot*2
###                   - undershoot2 = overshoot2
###
###                   - soft        = 0
###                   - soothe      = false
###                   - keep        = 25
###
###                   - edgemode    = 0
###                   - edgemaskHQ  = true
###
###                   - ss_x        = Smode==1?1.50:1.25
###                   - ss_y        = ss_x
###                   - dest_x      = ox
###                   - dest_y      = oy
###
###
### defaults="slow" : - strength    = 100
### ----------------- - Smode       = 2
###                   - Smethod     = 3
###                   - kernel      = 11
###
###                   - preblur     = -1
###                   - secure      = true
###                   - source      = undefined
###
###                   - Szrp        = 16
###                   - Spwr        = 4
###                   - SdmpLo      = 4
###                   - SdmpHi      = 48
###
###                   - Lmode       = 4
###                   - overshoot   = strength/100
###                   - undershoot  = overshoot
###                   - overshoot2  = overshoot*2
###                   - undershoot2 = overshoot2
###
###                   - soft        = -2
###                   - soothe      = true
###                   - keep        = 20
###
###                   - edgemode    = 0
###                   - edgemaskHQ  = true
###
###                   - ss_x        = Smode==3?1.00:1.50
###                   - ss_y        = ss_x
###                   - dest_x      = ox
###                   - dest_y      = oy
###
###
### defaults="fast" : - strength    = 80
### ----------------- - Smode       = 3
###                   - Smethod     = 2
###                   - kernel      = 11
###
###                   - preblur     = 0
###                   - secure      = true
###                   - source      = undefined
###
###                   - Szrp        = 16
###                   - Spwr        = 4
###                   - SdmpLo      = 4
###                   - SdmpHi      = 48
###
###                   - Lmode       = 0
###                   - overshoot   = strength/100
###                   - undershoot  = overshoot
###                   - overshoot2  = overshoot*2
###                   - undershoot2 = overshoot2
###
###                   - soft        = 0
###                   - soothe      = false
###                   - keep        = 20
###
###                   - edgemode    = 0
###                   - edgemaskHQ  = false
###
###                   - ss_x        = Smode==3?1.00:1.25
###                   - ss_y        = ss_x
###                   - dest_x      = ox
###                   - dest_y      = oy
###
################################################################################################
def LSFmod(input, strength=None, Smode=None, Smethod=None, kernel=11, preblur=None, secure=None, source=None, Szrp=16, Spwr=None, SdmpLo=None, SdmpHi=None, Lmode=None, overshoot=None, undershoot=None,
           overshoot2=None, undershoot2=None, soft=None, soothe=None, keep=None, edgemode=0, edgemaskHQ=None, ss_x=None, ss_y=None, dest_x=None, dest_y=None, defaults='fast'):
    if not isinstance(input, vs.VideoNode):
        raise vs.Error('LSFmod: This is not a clip')

    if input.format.color_family == vs.RGB:
        raise vs.Error('LSFmod: RGB format is not supported')

    if source is not None and (not isinstance(source, vs.VideoNode) or source.format.id != input.format.id):
        raise vs.Error("LSFmod: 'source' must be the same format as input")

    isGray = (input.format.color_family == vs.GRAY)
    isInteger = (input.format.sample_type == vs.INTEGER)

    if isInteger:
        neutral = 1 << (input.format.bits_per_sample - 1)
        peak = (1 << input.format.bits_per_sample) - 1
        factor = 1 << (input.format.bits_per_sample - 8)
    else:
        neutral = 0.0
        peak = 1.0
        factor = 255.0

    ### DEFAULTS
    try:
        num = ['old', 'slow', 'fast'].index(defaults.lower())
    except:
        raise vs.Error('LSFmod: Defaults must be "old" or "slow" or "fast"')

    ox = input.width
    oy = input.height

    if strength is None:
        strength = [100, 100, 80][num]
    if Smode is None:
        Smode = [1, 2, 3][num]
    if Smethod is None:
        Smethod = [2 if Smode == 1 else 1, 3, 2][num]
    if preblur is None:
        preblur = [-1, -1, 0][num]
    if secure is None:
        secure = [False, True, True][num]
    if Spwr is None:
        Spwr = [2, 4, 4][num]
    if SdmpLo is None:
        SdmpLo = [strength // 25, 4, 4][num]
    if SdmpHi is None:
        SdmpHi = [0, 48, 48][num]
    if Lmode is None:
        Lmode = [1, 4, 0][num]
    if overshoot is None:
        overshoot = [1, strength // 100, strength // 100][num]
    if undershoot is None:
        undershoot = overshoot
    if overshoot2 is None:
        overshoot2 = overshoot * 2
    if undershoot2 is None:
        undershoot2 = overshoot2
    if soft is None:
        soft = [0, -2, 0][num]
    if soothe is None:
        soothe = [False, True, False][num]
    if keep is None:
        keep = [25, 20, 20][num]
    if edgemaskHQ is None:
        edgemaskHQ = [True, True, False][num]
    if ss_x is None:
        ss_x = [1.5 if Smode == 1 else 1.25, 1.0 if Smode == 3 else 1.5, 1.0 if Smode == 3 else 1.25][num]
    if ss_y is None:
        ss_y = ss_x
    if dest_x is None:
        dest_x = ox
    if dest_y is None:
        dest_y = oy

    if kernel == 4:
        RemoveGrain = partial(core.std.Median)
    elif kernel in [11, 12]:
        RemoveGrain = partial(core.std.Convolution, matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1])
    elif kernel == 19:
        RemoveGrain = partial(core.std.Convolution, matrix=[1, 1, 1, 1, 0, 1, 1, 1, 1])
    elif kernel == 20:
        RemoveGrain = partial(core.std.Convolution, matrix=[1, 1, 1, 1, 1, 1, 1, 1, 1])
    else:
        RemoveGrain = partial(core.rgvs.RemoveGrain, mode=[kernel])

    if soft == -1:
        soft = math.sqrt(((ss_x + ss_y) / 2 - 1) * 100) * 10
    elif soft <= -2:
        soft = int((1 + 2 / (ss_x + ss_y)) * math.sqrt(strength))
    soft = min(soft, 100)

    xxs = cround(ox * ss_x / 8) * 8
    yys = cround(oy * ss_y / 8) * 8

    Str = strength / 100

    ### SHARP
    if ss_x > 1 or ss_y > 1:
        tmp = input.resize.Spline36(xxs, yys)
    else:
        tmp = input

    if not isGray:
        tmp_orig = tmp
        tmp = mvf.GetPlane(tmp, 0)

    if preblur <= -1:
        pre = tmp
    elif preblur >= 3:
        expr = 'x {i} < {peak} x {j} > 0 {peak} x {i} - {peak} {j} {i} - / * - ? ?'.format(i=scale(16, peak), j=scale(75, peak), peak=peak)
        pre = core.std.MaskedMerge(tmp.dfttest.DFTTest(tbsize=1, slocation=[0.0,4.0, 0.2,9.0, 1.0,15.0]), tmp, tmp.std.Expr(expr=[expr]))
    else:
        pre = MinBlur(tmp, r=preblur)

    dark_limit = pre.std.Minimum()
    bright_limit = pre.std.Maximum()

    if Smode < 3:
        if Smethod <= 1:
            method = RemoveGrain(pre)
        elif Smethod == 2:
            method = core.std.Merge(dark_limit, bright_limit)
        else:
            method = RemoveGrain(core.std.Merge(dark_limit, bright_limit))

        if secure:
            method = core.std.Expr([method, pre], expr=['x y < x {i} + x y > x {i} - x ? ?'.format(i=scale(1, peak))])

        if preblur > -1:
            method = core.std.MakeDiff(tmp, core.std.MakeDiff(pre, method))

        if Smode <= 1:
            normsharp = core.std.Expr([tmp, method], expr=[f'x x y - {Str} * +'])
        else:
            tmpScaled = tmp.std.Expr(expr=[f'x {1 / factor if isInteger else factor} *'], format=tmp.format.replace(sample_type=vs.FLOAT, bits_per_sample=32))
            methodScaled = method.std.Expr(expr=[f'x {1 / factor if isInteger else factor} *'], format=method.format.replace(sample_type=vs.FLOAT, bits_per_sample=32))
            expr = f'x y = x x x y - abs {Szrp} / {1 / Spwr} pow {Szrp} * {Str} * x y - dup abs / * x y - dup * {Szrp * Szrp} {SdmpLo} + * x y - dup * {SdmpLo} + {Szrp * Szrp} * / * 1 {SdmpHi} 0 = 0 {(Szrp / SdmpHi) ** 4} ? + 1 {SdmpHi} 0 = 0 x y - abs {SdmpHi} / 4 pow ? + / * + ? {factor if isInteger else 1 / factor} *'
            normsharp = core.std.Expr([tmpScaled, methodScaled], expr=[expr], format=tmp.format)
    else:
        normsharp = pre.cas.CAS(sharpness=min(Str, 1))

        if secure:
            normsharp = core.std.Expr([normsharp, pre], expr=['x y < x {i} + x y > x {i} - x ? ?'.format(i=scale(1, peak))])

        if preblur > -1:
            normsharp = core.std.MakeDiff(tmp, core.std.MakeDiff(pre, normsharp))

    ### LIMIT
    normal = Clamp(normsharp, bright_limit, dark_limit, scale(overshoot, peak), scale(undershoot, peak))
    second = Clamp(normsharp, bright_limit, dark_limit, scale(overshoot2, peak), scale(undershoot2, peak))
    zero = Clamp(normsharp, bright_limit, dark_limit, 0, 0)

    if edgemaskHQ:
        edge = tmp.std.Sobel(scale=2)
    else:
        edge = core.std.Expr([tmp.std.Maximum(), tmp.std.Minimum()], expr=['x y -'])
    edge = edge.std.Expr(expr=[f'x {1 / factor if isInteger else factor} * {128 if edgemaskHQ else 32} / 0.86 pow 255 * {factor if isInteger else 1 / factor} *'])

    if Lmode < 0:
        limit1 = core.rgvs.Repair(normsharp, tmp, mode=[abs(Lmode)])
    elif Lmode == 0:
        limit1 = normsharp
    elif Lmode == 1:
        limit1 = normal
    elif Lmode == 2:
        limit1 = core.std.MaskedMerge(normsharp, normal, edge.std.Inflate())
    elif Lmode == 3:
        limit1 = core.std.MaskedMerge(normal, zero, edge.std.Inflate())
    else:
        limit1 = core.std.MaskedMerge(second, normal, edge.std.Inflate())

    if edgemode <= 0:
        limit2 = limit1
    elif edgemode == 1:
        limit2 = core.std.MaskedMerge(tmp, limit1, edge.std.Inflate().std.Inflate().std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1]))
    else:
        limit2 = core.std.MaskedMerge(limit1, tmp, edge.std.Inflate().std.Inflate().std.Convolution(matrix=[1, 2, 1, 2, 4, 2, 1, 2, 1]))

    ### SOFT
    if soft == 0:
        PP1 = limit2
    else:
        sharpdiff = core.std.MakeDiff(tmp, limit2)
        sharpdiff = core.std.Expr([sharpdiff, sharpdiff.std.Convolution(matrix=[1, 1, 1, 1, 0, 1, 1, 1, 1])], expr=[f'x {neutral} - abs y {neutral} - abs > y {soft} * x {100 - soft} * + 100 / x ?'])
        PP1 = core.std.MakeDiff(tmp, sharpdiff)

    ### SOOTHE
    if soothe:
        diff = core.std.MakeDiff(tmp, PP1)
        diff = core.std.Expr([diff, AverageFrames(diff, weights=[1] * 3, scenechange=32 / 255)],
                             expr=[f'x {neutral} - y {neutral} - * 0 < x {neutral} - 100 / {keep} * {neutral} + x {neutral} - abs y {neutral} - abs > x {keep} * y {100 - keep} * + 100 / x ? ?'])
        PP2 = core.std.MakeDiff(tmp, diff)
    else:
        PP2 = PP1

    ### OUTPUT
    if dest_x != ox or dest_y != oy:
        if not isGray:
            PP2 = core.std.ShufflePlanes([PP2, tmp_orig], planes=[0, 1, 2], colorfamily=input.format.color_family)
        out = PP2.resize.Spline36(dest_x, dest_y)
    elif ss_x > 1 or ss_y > 1:
        out = PP2.resize.Spline36(dest_x, dest_y)
        if not isGray:
            out = core.std.ShufflePlanes([out, input], planes=[0, 1, 2], colorfamily=input.format.color_family)
    elif not isGray:
        out = core.std.ShufflePlanes([PP2, input], planes=[0, 1, 2], colorfamily=input.format.color_family)
    else:
        out = PP2

    if edgemode <= -1:
        return edge.resize.Spline36(dest_x, dest_y, format=input.format)
    elif source is not None:
        if dest_x != ox or dest_y != oy:
            src = source.resize.Spline36(dest_x, dest_y)
            In = input.resize.Spline36(dest_x, dest_y)
        else:
            src = source
            In = input

        shrpD = core.std.MakeDiff(In, out, planes=[0])
        expr = f'x {neutral} - abs y {neutral} - abs < x y ?'
        shrpL = core.std.Expr([core.rgvs.Repair(shrpD, core.std.MakeDiff(In, src, planes=[0]), mode=[1] if isGray else [1, 0]), shrpD], expr=[expr] if isGray else [expr, ''])
        return core.std.MakeDiff(In, shrpL, planes=[0])
    else:
        return out


########################
# Ported version of Temporal Degrain from avisynth
#
# Function by Sagekilla, idea + original script created by Didee
# ported by Hinterwaeldlers
#
# Works as a simple temporal degraining function that'll remove MOST grain
# from video sources, including dancing grain, like the grain found on 300.
# Also note, the parameters don't need to be tweaked much.
#
# Required vapoursynth plugins:
# * FFT3DFilter, if no pre calmed clip is given
# * MVTools
# * hqdn3d
#
# Note, the name of the parameters have been slightly changed from the avisynth
# variant, to hopefully clarify their meaning. In addition the GPU parameter is
# dropped, as there is no FFT3DFilter GPU variant.
#
# Usage:
# * inpClip:       The clip to process
# * denoiseClip:   An optional, pre calmed clip. This one should be "dead calm".
#                  If none is given, a clip is generated with FFT3DFilter
# * sigma:         FFT3DFilter filter strength
#                  Not used if a denoised clip is given
# * blockWidth:    FFT3DFilter block width
# * blockHeigth:   FFT3DFilter block width
# * sigma2:        FFT3DFilter sigma2 parameter
#                  Not used if a denoised clip is given
# * sigma3:        FFT3DFilter sigma3 parameter
#                  Not used if a denoised clip is given
# * sigma4:        FFT3DFilter sigma4 parameter
#                  Not used if a denoised clip is given
# * overlapWidth:  FFT3DFilter overlap width parameter
#                  Not used if a denoised clip is given
# * overlapHeight: FFT3DFilter overlap height parameter
#                  Not used if a denoised clip is given
# * blockSize:     MVTools Analyse block size
# * pel:           MVTools Analyse subpixel accurancy
# * overlapValue:  MVTools Analyse overlap parameter
# * degrain:       Amount of MVTools degrain vectors used.
#                  Valid values are: 1, 2 and 3
# * maxPxChange:   Limit the maximum allowed pixel change
# * thrDegrain1:   MVTools degrain SAD threshold in stage 1
# * thrDegrain2:   MVTools degrain SAD threshold in stage 2
# * HQ:            Adjust the filtering level:
#                  * 0: disable any prefiltering (not recommneded)
#                  * 1: extra prefiltering step
#                  * 2: extra pre- and postfiltering step

def TemporalDegrain(          \
      inpClip                 \
    , denoiseClip   = None    \
    , sigma         = 16      \
    , blockWidth    = 16      \
    , blockHeight   = 16      \
    , sigma2        = None    \
    , sigma3        = None    \
    , sigma4        = None    \
    , overlapWidth  = None    \
    , overlapHeight = None    \
    , blockSize     = 16      \
    , pel           = 2       \
    , overlapValue  = None    \
    , degrain       = 2       \
    , maxPxChange   = 255     \
    , thrDegrain1   = 400     \
    , thrDegrain2   = 300     \
    , HQ            = 1       \
) :

    if int(degrain) != degrain or degrain < 1 or degrain > 3:
        raise SyntaxError(\
            "Invalid degrain paramter! Must be 1, 2 or 3 (given as int)")

    neutral = 1 << (inpClip.format.bits_per_sample - 1)

    # Set the default value of not given values
    if sigma2 is None:
        sigma2 = sigma * 0.625
    if sigma3 is None:
        sigma3 = sigma * 0.375
    if sigma4 is None:
        sigma4 = sigma * 0.250
    if overlapWidth is None:
        overlapWidth = blockWidth // 2
    if overlapHeight is None:
        overlapHeight = blockHeight // 2
    if overlapValue is None:
        overlapValue = blockSize // 2

    # Input adjustments
    sigma2 = math.floor(sigma2)
    sigma3 = math.floor(sigma3)
    sigma4 = math.floor(sigma4)
    if overlapValue * 2 > blockSize:
        overlapValue = blockSize // 2

    # Taking care of a missing denoising clip and use of fft3d to determine it
    if denoiseClip is None:
        denoiseClip = inpClip.fft3dfilter.FFT3DFilter(sigma=sigma\
            , sigma2=sigma2, sigma3=sigma3, sigma4=sigma4, bw=blockWidth\
            , bh=blockHeight, ow=overlapWidth, oh=overlapHeight)

    # If HQ is activated, do an additional denoising
    if HQ > 0:
        filterClip = denoiseClip.hqdn3d.Hqdn3d(4,3,6,3)
    else:
        filterClip = denoiseClip

    # "spat" is a prefiltered clip which is used to limit the effect of the 1st
    # MV-denoise stage. For simplicity, we just use the same FFT3DFilter.
    # There's lots of other possibilities.
    spatD = core.std.MakeDiff(inpClip, filterClip)

    # Motion vector search (With very basic parameters. Add your own parameters
    # as needed.)
    srchSuper = filterClip.mv.Super(pel=pel)

    if degrain == 3:
        bvec3 = srchSuper.mv.Analyse(isb=True, delta=3, blksize=blockSize\
            , overlap=overlapValue)
        fvec3 = srchSuper.mv.Analyse(isb=False, delta=3, blksize=blockSize\
            , overlap=overlapValue)

    if degrain >= 2:
        bvec2 = srchSuper.mv.Analyse(isb=True, delta=2, blksize=blockSize\
            , overlap=overlapValue)
        fvec2 = srchSuper.mv.Analyse(isb=False, delta=2, blksize=blockSize\
            , overlap=overlapValue)

    bvec1 = srchSuper.mv.Analyse(isb=True, delta=1, blksize=blockSize\
        , overlap=overlapValue)
    fvec1 = srchSuper.mv.Analyse(isb=False, delta=1, blksize=blockSize\
        , overlap=overlapValue)

    # First MV-denoising stage. Usually here's some temporal-medianfiltering
    # going on. For simplicity, we just use MVDegrain.
    inpSuper = inpClip.mv.Super(pel=2, levels=1)
    if degrain == 3:
        nr1 = core.mv.Degrain3(inpClip, inpSuper, bvec1, fvec1, bvec2, fvec2\
            , bvec3, fvec3, thsad=thrDegrain1, limit=maxPxChange)
    elif degrain == 2:
        nr1 = core.mv.Degrain2(inpClip, inpSuper, bvec1, fvec1, bvec2, fvec2\
            , thsad=thrDegrain1, limit=maxPxChange)
    else:
        nr1 = core.mv.Degrain1(inpClip, inpSuper, bvec1, fvec1\
            , thsad=thrDegrain1, limit=maxPxChange)
    nr1Diff = core.std.MakeDiff(inpClip, nr1)

    # Limit NR1 to not do more than what "spat" would do.
    dd = core.std.Expr([spatD, nr1Diff], expr=[f'x {neutral} - abs y {neutral} - abs < x y ?'])
    nr1X = core.std.MakeDiff(inpClip, dd, planes=0)

    # Second MV-denoising stage
    nr1x_super = nr1X.mv.Super(pel=2, levels=1)

    if degrain == 3:
        nr2 = core.mv.Degrain3(nr1X, nr1x_super, bvec1, fvec1, bvec2, fvec2\
            , bvec3, fvec3, thsad=thrDegrain2, limit=maxPxChange)
    elif degrain == 2:
        nr2 = core.mv.Degrain2(nr1X, nr1x_super, bvec1, fvec1, bvec2, fvec2\
            , thsad=thrDegrain2, limit=maxPxChange)
    else:
        nr2 = core.mv.Degrain1(nr1X, nr1x_super, bvec1, fvec1\
            , thsad=thrDegrain2, limit=maxPxChange)

    # Temporal filter to remove the last bits of dancinc pixels, YMMV.
    if HQ >= 2:
        nr2 = nr2.hqdn3d.Hqdn3d(0,0,4,1)

    # Contra-sharpening: sharpen the denoised clip, but don't add more than
    # what was removed previously.
    # Here: A simple area-based version with relaxed restriction. The full
    # version is more complicated.
    return ContraSharpening(nr2, inpClip)


########################
# Ported version of aaf by MOmonster from avisynth
# Ported by Hinterwaeldlers
#
# aaf is one of the many aaa() modifications, so this is not my own basic idea
# the difference to aaa() is the repair postprocessing that allows also smaller sampling
# values without producing artefacts
# this makes aaf much faster (with small aas values)
#
# needed filters:
#	- MaskTools v2
#	- SangNom
#	- Repair (RemoveGrain pack)
#
# parameter description:
#	- mode
#			there are two modes you can use to reduce the side effects of sangnom
#			the default mode is "repair", it´s faster then the second mode="edge" and
#			avoid most artefacts also for smaller aas values
#			the mode "edge" filters only on edges and keep details sharper, read also estr/bstr
#			if you set another string than these two, no postprocessing will be done
#	- aas
#			this is the basic quality vs speed factor of aaf	->anti aliasing scaling
#			negative values process the horizontal and vertical direction without resizing
#			the complete source, this is much faster than with the absolut value, but will also
#			create more artefacts if you don´t use a repair mode
#			that higher the absolut value of aas that higher is the scaling factor, that better
#			is the quality, that slower the function and that lower the antialiasing effect
#			with aas=1.0 aaf performs like aa() and aaa()		[-2.0...2.0  -> -0.7]
#	- aay/aax
#			with aay and aax you can set the antialiasing strength in horizontal and vertical direction
#			if you set one of these parameter <=0 this direction won´t be processed
#			this give a nice speedup, but is only seldom useful	[0...64  ->28,aay]
#	- estr/bstr
#			these two parameters regulate the processing strength, they are only used with mode="edge"
#			estr is the strength on hard edges and bstr is the basic strength on flat areas
#			softer edges are calculated between these strength limits
#			estr has to be bigger than bstr		[0...255  ->255,40]

def aaf(                \
      inputClip         \
    , mode = "repair"   \
    , aas  = -0.7       \
    , aar  = None       \
    , aay  = 28         \
    , aax  = None       \
    , estr = 255        \
    , bstr = 40         \
) :
    mode = mode.lower()
    if aas < 0:
        aas = (aas-1)*0.25
    else:
        aas = (aas+1)*0.25
    # Determine the default parameters, which depend on other input
    if aar is None:
        aar = math.fabs(aas)
    if aax is None:
        aax = aay

    sx = inputClip.width
    sy = inputClip.height

    isGray = (inputClip.format.color_family == vs.GRAY)

    neutral = 1 << (inputClip.format.bits_per_sample - 1)
    peak = (1 << inputClip.format.bits_per_sample) - 1

    if aay > 0:
        # Do the upscaling
        if aas < 0:
            aa = inputClip.resize.Lanczos(sx, 4*int(sy*aar))
        elif aar == 0.5:
            aa = inputClip.resize.Point(2*sx, 2*sy)
        else:
            aa = inputClip.resize.Lanczos(4*int(sx*aar), 4*int(sy*aar))

        # y-Edges
        aa = aa.sangnom.SangNom(aa=aay)
    else:
        aa = inputClip

    if aax > 0:
        if aas < 0:
            aa = aa.resize.Lanczos(4*int(sx*aar), sy)
        aa = aa.std.Transpose()
        # x-Edges
        aa = aa.sangnom.SangNom(aa=aax)
        aa = aa.std.Transpose()

    # Restore original scaling
    aa = aa.resize.Lanczos(sx, sy)

    repMode = [18] if isGray else [18, 0]

    if mode == "repair":
        return core.rgvs.Repair(aa, inputClip, mode=repMode)

    if mode != "edge":
        return aa

    # u=1, v=1 is not directly so use the copy
    mask = core.std.MakeDiff(inputClip.std.Maximum(planes=0)\
                             , inputClip.std.Minimum(planes=0)\
                             , planes=0)
    expr = 'x {i} > {estr} x {neutral} - {j} 90 / * {bstr} + ?'.format(i=scale(218, peak), estr=scale(estr, peak), neutral=neutral, j=estr - bstr, bstr=scale(bstr, peak))
    mask = mask.std.Expr(expr=[expr] if isGray else [expr, ''])

    merged = core.std.MaskedMerge(inputClip, aa, mask, planes=0)
    if aas > 0.84:
        return merged
    return core.rgvs.Repair(merged, inputClip, mode=repMode)


#####################
#                   #
# Utility functions #
#                   #
#####################


def AverageFrames(clip, weights, scenechange=None, planes=None):
    if not isinstance(clip, vs.VideoNode):
        raise vs.Error('AverageFrames: This is not a clip')

    if scenechange:
        clip = SCDetect(clip, threshold=scenechange)
    return clip.misc.AverageFrames(weights=weights, scenechange=scenechange, planes=planes)


def AvsPrewitt(clip, planes=None):
    if not isinstance(clip, vs.VideoNode):
        raise vs.Error('AvsPrewitt: This is not a clip')

    if planes is None:
        planes = list(range(clip.format.num_planes))
    elif isinstance(planes, int):
        planes = [planes]

    return core.std.Expr([clip.std.Convolution(matrix=[1, 1, 0, 1, 0, -1, 0, -1, -1], planes=planes, saturate=False),
                          clip.std.Convolution(matrix=[1, 1, 1, 0, 0, 0, -1, -1, -1], planes=planes, saturate=False),
                          clip.std.Convolution(matrix=[1, 0, -1, 1, 0, -1, 1, 0, -1], planes=planes, saturate=False),
                          clip.std.Convolution(matrix=[0, -1, -1, 1, 0, -1, 1, 1, 0], planes=planes, saturate=False)],
                          expr=['x y max z max a max' if i in planes else '' for i in range(clip.format.num_planes)])


def Bob(clip, b=1/3, c=1/3, tff=None):
    if not isinstance(clip, vs.VideoNode):
        raise vs.Error('Bob: This is not a clip')

    if not isinstance(tff, bool):
        raise vs.Error("Bob: 'tff' must be set. Setting tff to true means top field first and false means bottom field first")

    bits_per_sample = clip.format.bits_per_sample
    clip = clip.std.SeparateFields(tff=tff).fmtc.resample(scalev=2, kernel='bicubic', a1=b, a2=c, interlaced=1, interlacedd=0)

    if clip.format.bits_per_sample != bits_per_sample:
        clip = clip.fmtc.bitdepth(bits=bits_per_sample, dmode=1)
    return clip


def ChangeFPS(clip, fpsnum, fpsden=1):
    if not isinstance(clip, vs.VideoNode):
        raise vs.Error('ChangeFPS: This is not a clip')

    factor = (fpsnum / fpsden) * (clip.fps_den / clip.fps_num)

    def frame_adjuster(n):
        real_n = math.floor(n / factor)
        one_frame_clip = clip[real_n] * (len(clip) + 100)
        return one_frame_clip

    attribute_clip = clip.std.BlankClip(length=math.floor(len(clip) * factor), fpsnum=fpsnum, fpsden=fpsden)
    return attribute_clip.std.FrameEval(eval=frame_adjuster)


def Clamp(clip, bright_limit, dark_limit, overshoot=0, undershoot=0, planes=None):
    if not (isinstance(clip, vs.VideoNode) and isinstance(bright_limit, vs.VideoNode) and isinstance(dark_limit, vs.VideoNode)):
        raise vs.Error('Clamp: This is not a clip')

    if bright_limit.format.id != clip.format.id or dark_limit.format.id != clip.format.id:
        raise vs.Error('Clamp: Clips must be the same format')

    if planes is None:
        planes = list(range(clip.format.num_planes))
    elif isinstance(planes, int):
        planes = [planes]

    expr = f'x y {overshoot} + > y {overshoot} + x ? z {undershoot} - < z {undershoot} - x y {overshoot} + > y {overshoot} + x ? ?'
    return core.std.Expr([clip, bright_limit, dark_limit], expr=[expr if i in planes else '' for i in range(clip.format.num_planes)])


def KNLMeansCL(clip, d=None, a=None, s=None, h=None, wmode=None, wref=None, device_type=None, device_id=None):
    if not isinstance(clip, vs.VideoNode):
        raise vs.Error('KNLMeansCL: This is not a clip')

    if clip.format.color_family not in [vs.YUV, vs.YCOCG]:
        raise vs.Error('KNLMeansCL: This wrapper is intended to be used only for YUV and YCoCg formats')

    if clip.format.subsampling_w > 0 or clip.format.subsampling_h > 0:
        return clip.knlm.KNLMeansCL(d=d, a=a, s=s, h=h, wmode=wmode, wref=wref, device_type=device_type, device_id=device_id).knlm.KNLMeansCL(
                     channels='UV', d=d, a=a, s=s, h=h, wmode=wmode, wref=wref, device_type=device_type, device_id=device_id)
    else:
        return clip.knlm.KNLMeansCL(d=d, a=a, s=s, h=h, channels='YUV', wmode=wmode, wref=wref, device_type=device_type, device_id=device_id)

# Available blend modes:
#  normal
#  addition
#  average
#  burn
#  darken
#  difference
#  divide
#  dodge
#  exclusion
#  extremity
#  freeze
#  glow
#  grainextract
#  grainmerge
#  hardlight
#  hardmix
#  heat
#  lighten
#  linearlight
#  multiply
#  negation
#  overlay
#  phoenix
#  pinlight
#  reflect
#  screen
#  softlight
#  subtract
#  vividlight
def Overlay(base, overlay, x=0, y=0, mask=None, opacity=1.0, mode='normal', planes=None, mask_first_plane=True):
    if not (isinstance(base, vs.VideoNode) and isinstance(overlay, vs.VideoNode)):
        raise vs.Error('Overlay: This is not a clip')

    if mask is not None:
        if not isinstance(mask, vs.VideoNode):
            raise vs.Error("Overlay: 'mask' is not a clip")

        if mask.width != overlay.width or mask.height != overlay.height or mask.format.bits_per_sample != overlay.format.bits_per_sample:
            raise vs.Error("Overlay: 'mask' must have the same dimensions and bit depth as 'overlay'")

    if base.format.sample_type == vs.INTEGER:
        neutral = 1 << (base.format.bits_per_sample - 1)
        peak = (1 << base.format.bits_per_sample) - 1
        factor = 1 << base.format.bits_per_sample
    else:
        neutral = 0.5
        peak = factor = 1.0

    if planes is None:
        planes = list(range(base.format.num_planes))
    elif isinstance(planes, int):
        planes = [planes]

    if base.format.subsampling_w > 0 or base.format.subsampling_h > 0:
        base_orig = base
        base = base.resize.Point(format=base.format.replace(subsampling_w=0, subsampling_h=0))
    else:
        base_orig = None

    if overlay.format.id != base.format.id:
        overlay = overlay.resize.Point(format=base.format)

    if mask is None:
        mask = overlay.std.BlankClip(format=overlay.format.replace(color_family=vs.GRAY, subsampling_w=0, subsampling_h=0), color=[peak])
    elif mask.format.id != overlay.format.id and mask.format.color_family != vs.GRAY:
        mask = mask.resize.Point(format=overlay.format, range_s='full')

    opacity = min(max(opacity, 0.0), 1.0)
    mode = mode.lower()

    # Calculate padding sizes
    l, r = x, base.width - overlay.width - x
    t, b = y, base.height - overlay.height - y

    # Split into crop and padding values
    cl, pl = min(l, 0) * -1, max(l, 0)
    cr, pr = min(r, 0) * -1, max(r, 0)
    ct, pt = min(t, 0) * -1, max(t, 0)
    cb, pb = min(b, 0) * -1, max(b, 0)

    # Crop and padding
    overlay = overlay.std.Crop(left=cl, right=cr, top=ct, bottom=cb)
    overlay = overlay.std.AddBorders(left=pl, right=pr, top=pt, bottom=pb)
    mask = mask.std.Crop(left=cl, right=cr, top=ct, bottom=cb)
    mask = mask.std.AddBorders(left=pl, right=pr, top=pt, bottom=pb, color=[0] * mask.format.num_planes)

    if opacity < 1:
        mask = mask.std.Expr(expr=[f'x {opacity} *'])

    if mode == 'normal':
        pass
    elif mode == 'addition':
        expr = f'x y +'
    elif mode == 'average':
        expr = f'x y + 2 /'
    elif mode == 'burn':
        expr = f'x 0 <= x {peak} {peak} y - {factor} * x / - ?'
    elif mode == 'darken':
        expr = f'x y min'
    elif mode == 'difference':
        expr = f'x y - abs'
    elif mode == 'divide':
        expr = f'y 0 <= {peak} {peak} x * y / ?'
    elif mode == 'dodge':
        expr = f'x {peak} >= x y {factor} * {peak} x - / ?'
    elif mode == 'exclusion':
        expr = f'x y + 2 x * y * {peak} / -'
    elif mode == 'extremity':
        expr = f'{peak} x - y - abs'
    elif mode == 'freeze':
        expr = f'y 0 <= 0 {peak} {peak} x - dup * y / {peak} min - ?'
    elif mode == 'glow':
        expr = f'x {peak} >= x y y * {peak} x - / ?'
    elif mode == 'grainextract':
        expr = f'x y - {neutral} +'
    elif mode == 'grainmerge':
        expr = f'x y + {neutral} -'
    elif mode == 'hardlight':
        expr = f'y {neutral} < 2 y x * {peak} / * {peak} 2 {peak} y - {peak} x - * {peak} / * - ?'
    elif mode == 'hardmix':
        expr = f'x {peak} y - < 0 {peak} ?'
    elif mode == 'heat':
        expr = f'x 0 <= 0 {peak} {peak} y - dup * x / {peak} min - ?'
    elif mode == 'lighten':
        expr = f'x y max'
    elif mode == 'linearlight':
        expr = f'y {neutral} < y 2 x * + {peak} - y 2 x {neutral} - * + ?'
    elif mode == 'multiply':
        expr = f'x y * {peak} /'
    elif mode == 'negation':
        expr = f'{peak} {peak} x - y - abs -'
    elif mode == 'overlay':
        expr = f'x {neutral} < 2 x y * {peak} / * {peak} 2 {peak} x - {peak} y - * {peak} / * - ?'
    elif mode == 'phoenix':
        expr = f'x y min x y max - {peak} +'
    elif mode == 'pinlight':
        expr = f'y {neutral} < x 2 y * min x 2 y {neutral} - * max ?'
    elif mode == 'reflect':
        expr = f'y {peak} >= y x x * {peak} y - / ?'
    elif mode =='screen':
        expr = f'{peak} {peak} x - {peak} y - * {peak} / -'
    elif mode == 'softlight':
        expr = f'x {neutral} > y {peak} y - x {neutral} - * {neutral} / 0.5 y {neutral} - abs {peak} / - * + y y {neutral} x - {neutral} / * 0.5 y {neutral} - abs {peak} / - * - ?'
    elif mode == 'subtract':
        expr = f'x y -'
    elif mode == 'vividlight':
        expr = f'x {neutral} < x 0 <= 2 x * {peak} {peak} y - {factor} * 2 x * / - ? 2 x {neutral} - * {peak} >= 2 x {neutral} - * y {factor} * {peak} 2 x {neutral} - * - / ? ?'
    else:
        raise vs.Error("Overlay: invalid 'mode' specified")

    if mode != 'normal':
        overlay = core.std.Expr([overlay, base], expr=[expr if i in planes else '' for i in range(base.format.num_planes)])
    last = core.std.MaskedMerge(base, overlay, mask, planes=planes, first_plane=mask_first_plane)

    # Return padded clip
    if base_orig is not None:
        last = core.resize.Point(last, format=base_orig.format)
    return last


def Padding(clip, left=0, right=0, top=0, bottom=0):
    if not isinstance(clip, vs.VideoNode):
        raise vs.Error('Padding: This is not a clip')

    if left < 0 or right < 0 or top < 0 or bottom < 0:
        raise vs.Error('Padding: border size to pad must not be negative')

    return clip.resize.Point(clip.width + left + right, clip.height + top + bottom, src_left=-left, src_top=-top, src_width=clip.width + left + right, src_height=clip.height + top + bottom)


def Resize(src, w, h, sx=None, sy=None, sw=None, sh=None, kernel=None, taps=None, a1=None, a2=None, invks=None, invkstaps=None, css=None, planes=None,
           center=None, cplace=None, cplaces=None, cplaced=None, interlaced=None, interlacedd=None, tff=None, tffd=None, flt=None, noring=False,
           bits=None, fulls=None, fulld=None, dmode=None, ampo=None, ampn=None, dyn=None, staticnoise=None, patsize=None):
    if not isinstance(src, vs.VideoNode):
        raise vs.Error('Resize: This is not a clip')

    if bits is None:
        bits = src.format.bits_per_sample

    sr_h = w / src.width
    sr_v = h / src.height
    sr_up = max(sr_h, sr_v)
    sr_dw = 1 / min(sr_h, sr_v)
    sr = max(sr_up, sr_dw)

    # Depending on the scale ratio, we may blend or totally disable the ringing cancellation
    thr = 2.5
    nrb = thr < sr < thr + 1
    nrf = sr < thr + 1 and noring

    main = src.fmtc.resample(w, h, sx, sy, sw, sh, kernel=kernel, taps=taps, a1=a1, a2=a2, invks=invks, invkstaps=invkstaps, css=css, planes=planes, center=center,
                             cplace=cplace, cplaces=cplaces, cplaced=cplaced, interlaced=interlaced, interlacedd=interlacedd, tff=tff, tffd=tffd, flt=flt)

    if nrf:
        nrng = src.fmtc.resample(w, h, sx, sy, sw, sh, kernel='gauss', taps=taps, a1=100, invks=invks, invkstaps=invkstaps, css=css, planes=planes, center=center,
                                 cplace=cplace, cplaces=cplaces, cplaced=cplaced, interlaced=interlaced, interlacedd=interlacedd, tff=tff, tffd=tffd, flt=flt)

        last = core.rgvs.Repair(main, nrng, mode=[1])
        if nrb:
            nr = sr - thr
            last = core.std.Merge(last, main, [nr])
    else:
        last = main

    if last.format.bits_per_sample == bits and fulls is None and fulld is None and dmode is None and ampo is None and ampn is None and dyn is None and staticnoise is None and patsize is None:
        return last
    else:
        planes2 = []
        if planes is None:
            for i in range(last.format.num_planes):
                planes2.append(i)
        else:
            if not isinstance(planes, list):
                planes = [planes]
            while len(planes) < last.format.num_planes:
                planes.append(planes[len(planes) - 1])
            for i in range(last.format.num_planes):
                if planes[i] != 1:
                    planes2.append(i)
        return last.fmtc.bitdepth(bits=bits, planes=planes2, fulls=fulls, fulld=fulld, dmode=dmode, ampo=ampo, ampn=ampn, dyn=dyn, staticnoise=staticnoise, patsize=patsize)


def SCDetect(clip, threshold=None):
    def copy_property(n, f):
        fout = f[0].copy()
        fout.props['_SceneChangePrev'] = f[1].props['_SceneChangePrev']
        fout.props['_SceneChangeNext'] = f[1].props['_SceneChangeNext']
        return fout

    if not isinstance(clip, vs.VideoNode):
        raise vs.Error('SCDetect: This is not a clip')

    sc = clip
    if clip.format.color_family == vs.RGB:
        sc = clip.resize.Bicubic(format=vs.GRAY8, matrix_s='709')
    sc = sc.misc.SCDetect(threshold=threshold)

    if clip.format.color_family == vs.RGB:
        sc = clip.std.ModifyFrame(clips=[clip, sc], selector=copy_property)
    return sc


def Weave(clip, tff):
    if not isinstance(clip, vs.VideoNode):
        raise vs.Error('Weave: This is not a clip')

    return clip.std.DoubleWeave(tff=tff)[::2]


########################################
## Didée's functions:

# contra-sharpening: sharpen the denoised clip, but don't add more to any pixel than what was removed previously.
# script function from Didée, at the VERY GRAINY thread (http://forum.doom9.org/showthread.php?p=1076491#post1076491)
#
# Parameters:
#  radius (int)   - Spatial radius for contra-sharpening (1-3). Default is 2 for HD / 1 for SD
#  rep (int)      - Mode of repair to limit the difference. Default is 13
#  planes (int[]) - Whether to process the corresponding plane. The other planes will be passed through unchanged.
def ContraSharpening(denoised, original, radius=None, rep=13, planes=None):
    if not (isinstance(denoised, vs.VideoNode) and isinstance(original, vs.VideoNode)):
        raise vs.Error('ContraSharpening: This is not a clip')

    if denoised.format.id != original.format.id:
        raise vs.Error('ContraSharpening: Clips must be the same format')

    neutral = 1 << (denoised.format.bits_per_sample - 1)

    if planes is None:
        planes = list(range(denoised.format.num_planes))
    elif isinstance(planes, int):
        planes = [planes]

    if radius is None:
        radius = 2 if denoised.width > 1024 or denoised.height > 576 else 1

    s = MinBlur(denoised, r=radius, planes=planes)                                                                   # damp down remaining spots of the denoised clip

    matrix1 = [1, 2, 1, 2, 4, 2, 1, 2, 1]
    matrix2 = [1, 1, 1, 1, 1, 1, 1, 1, 1]

    if radius <= 1:
        RG11 = s.std.Convolution(matrix=matrix1, planes=planes)
    elif radius == 2:
        RG11 = s.std.Convolution(matrix=matrix1, planes=planes).std.Convolution(matrix=matrix2, planes=planes)
    else:
        RG11 = s.std.Convolution(matrix=matrix1, planes=planes).std.Convolution(matrix=matrix2, planes=planes).std.Convolution(matrix=matrix2, planes=planes)

    ssD = core.std.MakeDiff(s, RG11, planes=planes)                                                                  # the difference of a simple kernel blur
    allD = core.std.MakeDiff(original, denoised, planes=planes)                                                      # the difference achieved by the denoising
    ssDD = core.rgvs.Repair(ssD, allD, mode=[rep if i in planes else 0 for i in range(denoised.format.num_planes)])  # limit the difference to the max of what the denoising removed locally
    expr = f'x {neutral} - abs y {neutral} - abs < x y ?'
    ssDD = core.std.Expr([ssDD, ssD], expr=[expr if i in planes else '' for i in range(denoised.format.num_planes)]) # abs(diff) after limiting may not be bigger than before
    return core.std.MergeDiff(denoised, ssDD, planes=planes)                                                         # apply the limited difference (sharpening is just inverse blurring)


# MinBlur   by Didée (http://avisynth.nl/index.php/MinBlur)
# Nifty Gauss/Median combination
def MinBlur(clp, r=1, planes=None):
    if not isinstance(clp, vs.VideoNode):
        raise vs.Error('MinBlur: This is not a clip')

    if planes is None:
        planes = list(range(clp.format.num_planes))
    elif isinstance(planes, int):
        planes = [planes]

    matrix1 = [1, 2, 1, 2, 4, 2, 1, 2, 1]
    matrix2 = [1, 1, 1, 1, 1, 1, 1, 1, 1]

    if r <= 0:
        RG11 = sbr(clp, planes=planes)
        RG4 = clp.std.Median(planes=planes)
    elif r == 1:
        RG11 = clp.std.Convolution(matrix=matrix1, planes=planes)
        RG4 = clp.std.Median(planes=planes)
    elif r == 2:
        RG11 = clp.std.Convolution(matrix=matrix1, planes=planes).std.Convolution(matrix=matrix2, planes=planes)
        RG4 = clp.ctmf.CTMF(radius=2, planes=planes)
    else:
        RG11 = clp.std.Convolution(matrix=matrix1, planes=planes).std.Convolution(matrix=matrix2, planes=planes).std.Convolution(matrix=matrix2, planes=planes)
        if clp.format.bits_per_sample == 16:
            s16 = clp
            RG4 = clp.fmtc.bitdepth(bits=12, planes=planes, dmode=1).ctmf.CTMF(radius=3, planes=planes).fmtc.bitdepth(bits=16, planes=planes)
            RG4 = mvf.LimitFilter(s16, RG4, thr=0.0625, elast=2, planes=planes)
        else:
            RG4 = clp.ctmf.CTMF(radius=3, planes=planes)

    expr = 'x y - x z - * 0 < x x y - abs x z - abs < y z ? ?'
    return core.std.Expr([clp, RG11, RG4], expr=[expr if i in planes else '' for i in range(clp.format.num_planes)])


# make a highpass on a blur's difference (well, kind of that)
def sbr(c, r=1, planes=None):
    if not isinstance(c, vs.VideoNode):
        raise vs.Error('sbr: This is not a clip')

    neutral = 1 << (c.format.bits_per_sample - 1) if c.format.sample_type == vs.INTEGER else 0.0

    if planes is None:
        planes = list(range(c.format.num_planes))
    elif isinstance(planes, int):
        planes = [planes]

    matrix1 = [1, 2, 1, 2, 4, 2, 1, 2, 1]
    matrix2 = [1, 1, 1, 1, 1, 1, 1, 1, 1]

    if r <= 1:
        RG11 = c.std.Convolution(matrix=matrix1, planes=planes)
    elif r == 2:
        RG11 = c.std.Convolution(matrix=matrix1, planes=planes).std.Convolution(matrix=matrix2, planes=planes)
    else:
        RG11 = c.std.Convolution(matrix=matrix1, planes=planes).std.Convolution(matrix=matrix2, planes=planes).std.Convolution(matrix=matrix2, planes=planes)

    RG11D = core.std.MakeDiff(c, RG11, planes=planes)

    if r <= 1:
        RG11DS = RG11D.std.Convolution(matrix=matrix1, planes=planes)
    elif r == 2:
        RG11DS = RG11D.std.Convolution(matrix=matrix1, planes=planes).std.Convolution(matrix=matrix2, planes=planes)
    else:
        RG11DS = RG11D.std.Convolution(matrix=matrix1, planes=planes).std.Convolution(matrix=matrix2, planes=planes).std.Convolution(matrix=matrix2, planes=planes)

    expr = f'x y - x {neutral} - * 0 < {neutral} x y - abs x {neutral} - abs < x y - {neutral} + x ? ?'
    RG11DD = core.std.Expr([RG11D, RG11DS], expr=[expr if i in planes else '' for i in range(c.format.num_planes)])
    return core.std.MakeDiff(c, RG11DD, planes=planes)


def sbrV(c, r=1, planes=None):
    if not isinstance(c, vs.VideoNode):
        raise vs.Error('sbrV: This is not a clip')

    neutral = 1 << (c.format.bits_per_sample - 1) if c.format.sample_type == vs.INTEGER else 0.0

    if planes is None:
        planes = list(range(c.format.num_planes))
    elif isinstance(planes, int):
        planes = [planes]

    matrix1 = [1, 2, 1]
    matrix2 = [1, 4, 6, 4, 1]

    if r <= 1:
        RG11 = c.std.Convolution(matrix=matrix1, planes=planes, mode='v')
    elif r == 2:
        RG11 = c.std.Convolution(matrix=matrix1, planes=planes, mode='v').std.Convolution(matrix=matrix2, planes=planes, mode='v')
    else:
        RG11 = c.std.Convolution(matrix=matrix1, planes=planes, mode='v').std.Convolution(matrix=matrix2, planes=planes, mode='v').std.Convolution(matrix=matrix2, planes=planes, mode='v')

    RG11D = core.std.MakeDiff(c, RG11, planes=planes)

    if r <= 1:
        RG11DS = RG11D.std.Convolution(matrix=matrix1, planes=planes, mode='v')
    elif r == 2:
        RG11DS = RG11D.std.Convolution(matrix=matrix1, planes=planes, mode='v').std.Convolution(matrix=matrix2, planes=planes, mode='v')
    else:
        RG11DS = RG11D.std.Convolution(matrix=matrix1, planes=planes, mode='v').std.Convolution(matrix=matrix2, planes=planes, mode='v').std.Convolution(matrix=matrix2, planes=planes, mode='v')

    expr = f'x y - x {neutral} - * 0 < {neutral} x y - abs x {neutral} - abs < x y - {neutral} + x ? ?'
    RG11DD = core.std.Expr([RG11D, RG11DS], expr=[expr if i in planes else '' for i in range(c.format.num_planes)])
    return core.std.MakeDiff(c, RG11DD, planes=planes)


########################################
## cretindesalpes' functions:

# Converts luma (and chroma) to PC levels, and optionally allows tweaking for pumping up the darks. (for the clip to be fed to motion search only)
# By courtesy of cretindesalpes. (http://forum.doom9.org/showthread.php?p=1548318#post1548318)
def DitherLumaRebuild(src, s0=2.0, c=0.0625, chroma=True):
    if not isinstance(src, vs.VideoNode):
        raise vs.Error('DitherLumaRebuild: This is not a clip')

    if src.format.color_family == vs.RGB:
        raise vs.Error('DitherLumaRebuild: RGB format is not supported')

    isGray = (src.format.color_family == vs.GRAY)
    isInteger = (src.format.sample_type == vs.INTEGER)

    shift = src.format.bits_per_sample - 8
    neutral = 128 << shift if isInteger else 0.0

    k = (s0 - 1) * c
    t = f'x {16 << shift if isInteger else 16 / 255} - {219 << shift if isInteger else 219 / 255} / 0 max 1 min'
    e = f'{k} {1 + c} {(1 + c) * c} {t} {c} + / - * {t} 1 {k} - * + {256 << shift if isInteger else 256 / 255} *'
    return src.std.Expr(expr=[e] if isGray else [e, f'x {neutral} - 128 * 112 / {neutral} +' if chroma else ''])


#=============================================================================
#   mt_expand_multi
#   mt_inpand_multi
#
#   Calls mt_expand or mt_inpand multiple times in order to grow or shrink
#   the mask from the desired width and height.
#
#   Parameters:
#   - sw   : Growing/shrinking shape width. 0 is allowed. Default: 1
#   - sh   : Growing/shrinking shape height. 0 is allowed. Default: 1
#   - mode : "rectangle" (default), "ellipse" or "losange". Replaces the
#       mt_xxpand mode. Ellipses are actually combinations of
#       rectangles and losanges and look more like octogons.
#       Losanges are truncated (not scaled) when sw and sh are not
#       equal.
#   Other parameters are the same as mt_xxpand.
#=============================================================================
def mt_expand_multi(src, mode='rectangle', planes=None, sw=1, sh=1):
    if not isinstance(src, vs.VideoNode):
        raise vs.Error('mt_expand_multi: This is not a clip')

    if sw > 0 and sh > 0:
        mode_m = [0, 1, 0, 1, 1, 0, 1, 0] if mode == 'losange' or (mode == 'ellipse' and (sw % 3) != 1) else [1, 1, 1, 1, 1, 1, 1, 1]
    elif sw > 0:
        mode_m = [0, 0, 0, 1, 1, 0, 0, 0]
    elif sh > 0:
        mode_m = [0, 1, 0, 0, 0, 0, 1, 0]
    else:
        mode_m = None

    if mode_m is not None:
        src = mt_expand_multi(src.std.Maximum(planes=planes, coordinates=mode_m), mode=mode, planes=planes, sw=sw - 1, sh=sh - 1)
    return src


def mt_inpand_multi(src, mode='rectangle', planes=None, sw=1, sh=1):
    if not isinstance(src, vs.VideoNode):
        raise vs.Error('mt_inpand_multi: This is not a clip')

    if sw > 0 and sh > 0:
        mode_m = [0, 1, 0, 1, 1, 0, 1, 0] if mode == 'losange' or (mode == 'ellipse' and (sw % 3) != 1) else [1, 1, 1, 1, 1, 1, 1, 1]
    elif sw > 0:
        mode_m = [0, 0, 0, 1, 1, 0, 0, 0]
    elif sh > 0:
        mode_m = [0, 1, 0, 0, 0, 0, 1, 0]
    else:
        mode_m = None

    if mode_m is not None:
        src = mt_inpand_multi(src.std.Minimum(planes=planes, coordinates=mode_m), mode=mode, planes=planes, sw=sw - 1, sh=sh - 1)
    return src


def mt_inflate_multi(src, planes=None, radius=1):
    if not isinstance(src, vs.VideoNode):
        raise vs.Error('mt_inflate_multi: This is not a clip')

    for i in range(radius):
        src = core.std.Inflate(src, planes=planes)
    return src


def mt_deflate_multi(src, planes=None, radius=1):
    if not isinstance(src, vs.VideoNode):
        raise vs.Error('mt_deflate_multi: This is not a clip')

    for i in range(radius):
        src = core.std.Deflate(src, planes=planes)
    return src


####################
#                  #
# Helper functions #
#                  #
####################


def cround(x):
    return math.floor(x + 0.5) if x > 0 else math.ceil(x - 0.5)


def m4(x):
    return 16 if x < 16 else cround(x / 4) * 4


def scale(value, peak):
    return cround(value * peak / 255) if peak != 1 else value / 255
