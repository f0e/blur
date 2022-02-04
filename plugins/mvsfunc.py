################################################################################################################################
## mvsfunc - mawen1250's VapourSynth functions
## 2016.07
################################################################################################################################
## Requirments:
##     fmtconv
##     BM3D
################################################################################################################################
## Main functions:
##     Depth
##     ToRGB
##     ToYUV
##     BM3D
##     VFRSplice
################################################################################################################################
## Runtime functions:
##     PlaneStatistics
##     PlaneCompare
##     ShowAverage
##     FilterIf
##     FilterCombed
################################################################################################################################
## Utility functions:
##     Min
##     Max
##     Avg
##     MinFilter
##     MaxFilter
##     LimitFilter
##     PointPower
##     CheckMatrix
##     postfix2infix
################################################################################################################################
## Frame property functions:
##     SetColorSpace
##     AssumeFrame
##     AssumeTFF
##     AssumeBFF
##     AssumeField
##     AssumeCombed
################################################################################################################################
## Helper functions:
##     CheckVersion
##     GetMatrix
##     zDepth
##     PlaneAverage
##     GetPlane
##     GrayScale
##     Preview
################################################################################################################################


import vapoursynth as vs
import functools
import math


################################################################################################################################


MvsFuncVersion = 8
VSMaxPlaneNum = 3


################################################################################################################################





################################################################################################################################
################################################################################################################################
################################################################################################################################
## Main functions below
################################################################################################################################
################################################################################################################################
################################################################################################################################


################################################################################################################################
## Main function: Depth()
################################################################################################################################
## Bit depth conversion with dithering (if needed).
## It's a wrapper for fmtc.bitdepth and zDepth(core.resize/zimg).
## Only constant format is supported, frame properties of the input clip is mostly ignored (only zDepth is able to use it).
################################################################################################################################
## Basic parameters
##     input {clip}: clip to be converted
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 16/32 bit float
##     depth {int}: output bit depth, can be 1-16 bit integer or 16/32 bit float
##         note that 1-7 bit content is still stored as 8 bit integer format
##         default is the same as that of the input clip
##     sample {int}: output sample type, can be 0(vs.INTEGER) or 1(vs.FLOAT)
##         default is the same as that of the input clip
##     fulls {bool}: define if input clip is of full range
##         default: None, assume True for RGB/YCgCo input, assume False for Gray/YUV input
##     fulld {bool}: define if output clip is of full range
##         default is the same as "fulls"
################################################################################################################################
## Advanced parameters
##     dither {int|str}: dithering algorithm applied for depth conversion
##         - {int}: same as "dmode" in fmtc.bitdepth, will be automatically converted if using zDepth
##         - {str}: same as "dither" in zDepth, will be automatically converted if using fmtc.bitdepth
##         - default:
##             - output depth is 32, and conversions without quantization error: 1 | "none"
##             - otherwise: 3 | "error_diffusion"
##     useZ {bool}: prefer zDepth or fmtc.bitdepth for depth conversion
##         When 13-,15-bit integer or 16-bit float is involved, zDepth is always used.
##         - False: prefer fmtc.bitdepth
##         - True: prefer zDepth
##         default: False
##     prefer_props {bool}: determines whether frame properties or arguments take precedence when both are present
##         For now, it only makes sense when zDepth is involved and only affects the _ColorRange property.
##         - False: prefer arguments
##         - True: prefer frame properties
##         default: False
################################################################################################################################
## Parameters of fmtc.bitdepth
##     ampo, ampn, dyn, staticnoise: same as those in fmtc.bitdepth, ignored when using zDepth
################################################################################################################################
def Depth(input, depth=None, sample=None, fulls=None, fulld=None, \
dither=None, useZ=None, prefer_props=None, ampo=None, ampn=None, dyn=None, staticnoise=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'Depth'
    clip = input
    
    if not isinstance(input, vs.VideoNode):
        raise TypeError(funcName + ': \"input\" must be a clip!')
    
    prefer_props_range = None
    
    # Get properties of input clip
    sFormat = input.format
    
    sColorFamily = sFormat.color_family
    sIsRGB = sColorFamily == vs.RGB
    sIsYUV = sColorFamily == vs.YUV
    sIsGRAY = sColorFamily == vs.GRAY
    sIsYCOCG = sColorFamily == vs.YCOCG
    if sColorFamily == vs.COMPAT:
        raise ValueError(funcName + ': color family *COMPAT* is not supported!')
    
    sbitPS = sFormat.bits_per_sample
    sSType = sFormat.sample_type
    
    if fulls is None:
        # If not set, assume limited range for YUV and Gray input
        fulls = False if sIsYUV or sIsGRAY else True
    elif not isinstance(fulls, int):
        raise TypeError(funcName + ': \"fulls\" must be a bool!')
    
    # Get properties of output clip
    lowDepth = False
    
    if depth is None:
        dbitPS = sbitPS
    elif not isinstance(depth, int):
        raise TypeError(funcName + ': \"depth\" must be an int!')
    else:
        if depth < 8:
            dbitPS = 8
            lowDepth = True
        else:
            dbitPS = depth
    if sample is None:
        if depth is None:
            dSType = sSType
            depth = dbitPS
        else:
            dSType = vs.FLOAT if dbitPS >= 32 else vs.INTEGER
    elif not isinstance(sample, int):
        raise TypeError(funcName + ': \"sample\" must be an int!')
    elif sample != vs.INTEGER and sample != vs.FLOAT:
        raise ValueError(funcName + ': \"sample\" must be either 0(vs.INTEGER) or 1(vs.FLOAT)!')
    else:
        dSType = sample
    if depth is None and sSType != vs.FLOAT and sample == vs.FLOAT:
        dbitPS = 32
    elif depth is None and sSType != vs.INTEGER and sample == vs.INTEGER:
        dbitPS = 16
    if dSType == vs.INTEGER and (dbitPS < 1 or dbitPS > 16):
        raise ValueError(funcName + ': {0}-bit integer output is not supported!'.format(dbitPS))
    if dSType == vs.FLOAT and (dbitPS != 16 and dbitPS != 32):
        raise ValueError(funcName + ': {0}-bit float output is not supported!'.format(dbitPS))
    
    if fulld is None:
        fulld = fulls
    elif not isinstance(fulld, int):
        raise TypeError(funcName + ': \"fulld\" must be a bool!')
    
    # Low-depth support
    if lowDepth:
        if dither == "none" or dither == 1:
            clip = _quantization_conversion(clip, sbitPS, depth, vs.INTEGER, fulls, fulld, False, False, 8, 0, funcName)
            clip = _quantization_conversion(clip, depth, 8, vs.INTEGER, fulld, fulld, False, False, 8, 0, funcName)
            return clip
        else:
            full = fulld
            clip = _quantization_conversion(clip, sbitPS, depth, vs.INTEGER, fulls, full, False, False, 16, 1, funcName)
            sSType = vs.INTEGER
            sbitPS = 16
            fulls = False
            fulld = False
    
    # Whether to use zDepth or fmtc.bitdepth for conversion
    # When 13-,15-bit integer or 16-bit float format is involved, force using zDepth
    if useZ is None:
        useZ = False
    elif not isinstance(useZ, int):
        raise TypeError(funcName + ': \"useZ\" must be a bool!')
    if sSType == vs.INTEGER and (sbitPS == 13 or sbitPS == 15):
        useZ = True
    if dSType == vs.INTEGER and (dbitPS == 13 or dbitPS == 15):
        useZ = True
    if (sSType == vs.FLOAT and sbitPS < 32) or (dSType == vs.FLOAT and dbitPS < 32):
        useZ = True
    
    if prefer_props is None:
        prefer_props = False
    elif not isinstance(prefer_props, int):
        raise TypeError(funcName + ': \"prefer_props\" must be a bool!')
    if prefer_props_range is None:
        prefer_props_range = prefer_props
    
    # Dithering type
    if ampn is not None and not isinstance(ampn, float) and not isinstance(ampn, int):
            raise TypeError(funcName + ': \"ampn\" must be an int or a float!')
    
    if dither is None:
        if dbitPS == 32 or (dbitPS >= sbitPS and fulld == fulls and fulld == False):
            dither = "none" if useZ else 1
        else:
            dither = "error_diffusion" if useZ else 3
    elif not isinstance(dither, int) and not isinstance(dither, str):
        raise TypeError(funcName + ': \"dither\" must be an int or a str!')
    else:
        if isinstance(dither, str):
            dither = dither.lower()
            if dither != "none" and dither != "ordered" and dither != "random" and dither != "error_diffusion":
                raise ValueError(funcName + ': Unsupported \"dither\" specified!')
        else:
            if dither < 0 or dither > 7:
                raise ValueError(funcName + ': Unsupported \"dither\" specified!')
        if useZ and isinstance(dither, int):
            if dither == 0:
                dither = "ordered"
            elif dither == 1 or dither == 2:
                if ampn is not None and ampn > 0:
                    dither = "random"
                else:
                    dither = "none"
            else:
                dither = "error_diffusion"
        elif not useZ and isinstance(dither, str):
            if dither == "none":
                dither = 1
            elif dither == "ordered":
                dither = 0
            elif dither == "random":
                if ampn is None:
                    dither = 1
                    ampn = 1
                elif ampn > 0:
                    dither = 1
                else:
                    dither = 3
            else:
                dither = 3
    
    if not useZ:
        if ampo is None:
            ampo = 1.5 if dither == 0 else 1
        elif not isinstance(ampo, float) and not isinstance(ampo, int):
            raise TypeError(funcName + ': \"ampo\" must be an int or a float!')
    
    # Skip processing if not needed
    if dSType == sSType and dbitPS == sbitPS and (sSType == vs.FLOAT or (fulld == fulls and not prefer_props_range)) and not lowDepth:
        return clip
    
    # Apply conversion
    if useZ:
        clip = zDepth(clip, sample=dSType, depth=dbitPS, range=fulld, range_in=fulls, dither_type=dither, prefer_props=prefer_props_range)
    else:
        clip = core.fmtc.bitdepth(clip, bits=dbitPS, flt=dSType, fulls=fulls, fulld=fulld, dmode=dither, ampo=ampo, ampn=ampn, dyn=dyn, staticnoise=staticnoise)
        clip = SetColorSpace(clip, ColorRange=0 if fulld else 1)
    
    # Low-depth support
    if lowDepth:
        clip = _quantization_conversion(clip, depth, 8, vs.INTEGER, full, full, False, False, 8, 0, funcName)
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Main function: ToRGB()
################################################################################################################################
## Convert any color space to full range RGB.
## Thus, if input is limited range RGB, it will be converted to full range.
## If matrix is 10, "2020cl" or "bt2020c", the output is linear RGB.
## It's mainly a wrapper for fmtconv.
## Only constant format is supported, frame properties of the input clip is mostly ignored.
## Note that you may get faster speed with core.resize, or not (for now, dither_type='error_diffusion' is slow).
## It's recommended to use Preview() for previewing now.
################################################################################################################################
## Basic parameters
##     input {clip}: clip to be converted
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 16/32 bit float
##     matrix {int|str}: color matrix of input clip, only makes sense for YUV/YCoCg input
##         decides the conversion coefficients from YUV to RGB
##         check GetMatrix() for available values
##         default: None, guessed according to the color family and size of input clip
##     depth {int}: output bit depth, can be 1-16 bit integer or 16/32 bit float
##         note that 1-7 bit content is still stored as 8 bit integer format
##         default is the same as that of the input clip
##     sample {int}: output sample type, can be 0(vs.INTEGER) or 1(vs.FLOAT)
##         default is the same as that of the input clip
##     full {bool}: define if input clip is of full range
##         default: guessed according to the color family of input clip and "matrix"
##     compat {bool}: force CompatBGR32 output, "depth" will be forced to 8
##         default: False
################################################################################################################################
## Parameters of depth conversion
##     dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise:
##         same as those in Depth()
################################################################################################################################
## Parameters of resampling
##     kernel, taps, a1, a2, cplace:
##         used for chroma re-sampling, same as those in fmtc.resample
##         default: kernel="bicubic", a1=0, a2=0.5, also known as "Catmull-Rom".
################################################################################################################################
def ToRGB(input, matrix=None, depth=None, sample=None, full=None, \
dither=None, useZ=None, prefer_props=None, ampo=None, ampn=None, dyn=None, staticnoise=None, \
kernel=None, taps=None, a1=None, a2=None, cplace=None, \
compat=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'ToRGB'
    clip = input
    
    if not isinstance(input, vs.VideoNode):
        raise TypeError(funcName + ': \"input\" must be a clip!')
    
    # Get string format parameter "matrix"
    matrix = GetMatrix(input, matrix, True)
    
    # Get properties of input clip
    sFormat = input.format
    
    sColorFamily = sFormat.color_family
    sIsRGB = sColorFamily == vs.RGB
    sIsYUV = sColorFamily == vs.YUV
    sIsGRAY = sColorFamily == vs.GRAY
    sIsYCOCG = sColorFamily == vs.YCOCG
    if sColorFamily == vs.COMPAT:
        raise ValueError(funcName + ': color family *COMPAT* is not supported!')
    
    sbitPS = sFormat.bits_per_sample
    sSType = sFormat.sample_type
    
    sHSubS = 1 << sFormat.subsampling_w
    sVSubS = 1 << sFormat.subsampling_h
    
    if full is None:
        # If not set, assume limited range for YUV and Gray input
        # Assume full range for YCgCo and OPP input
        if (sIsGRAY or sIsYUV or sIsYCOCG) and (matrix == "RGB" or matrix == "YCgCo" or matrix == "OPP"):
            fulls = True
        else:
            fulls = False if sIsYUV or sIsGRAY else True
    elif not isinstance(full, int):
        raise TypeError(funcName + ': \"full\" must be a bool!')
    else:
        fulls = full
    
    # Get properties of output clip
    if compat is None:
        compat = False
    elif not isinstance(compat, int):
        raise TypeError(funcName + ': \"compat\" must be an int!')
    if compat:
        depth = 8
        sample = vs.INTEGER
    if depth is None:
        dbitPS = sbitPS
    elif not isinstance(depth, int):
        raise TypeError(funcName + ': \"depth\" must be an int!')
    else:
        dbitPS = depth
    if sample is None:
        if depth is None:
            dSType = sSType
        else:
            dSType = vs.FLOAT if dbitPS >= 32 else vs.INTEGER
    elif not isinstance(sample, int):
        raise TypeError(funcName + ': \"sample\" must be an int!')
    elif sample != vs.INTEGER and sample != vs.FLOAT:
        raise ValueError(funcName + ': \"sample\" must be either 0(vs.INTEGER) or 1(vs.FLOAT)!')
    else:
        dSType = sample
    if depth is None and sSType != vs.FLOAT and sample == vs.FLOAT:
        dbitPS = 32
    elif depth is None and sSType != vs.INTEGER and sample == vs.INTEGER:
        dbitPS = 16
    if dSType == vs.INTEGER and (dbitPS < 1 or dbitPS > 16):
        raise ValueError(funcName + ': {0}-bit integer output is not supported!'.format(dbitPS))
    if dSType == vs.FLOAT and (dbitPS != 16 and dbitPS != 32):
        raise ValueError(funcName + ': {0}-bit float output is not supported!'.format(dbitPS))
    
    fulld = True
    
    # Get properties of internal processed clip
    pSType = max(sSType, dSType) # If float sample type is involved, then use float for conversion
    if pSType == vs.FLOAT:
        # For float sample type, only 32-bit is supported by fmtconv
        pbitPS = 32
    else:
        # Apply conversion in the higher one of input and output bit depth
        pbitPS = max(sbitPS, dbitPS)
        # For integer sample type, only 8-, 9-, 10-, 12-, 16-bit is supported by fmtc.matrix
        if sHSubS != 1 or sVSubS != 1:
            # When chroma re-sampling is needed, always process in 16-bit for integer sample type
            pbitPS = 16
        elif pbitPS == 11:
            pbitPS = 12
        elif pbitPS > 12 and pbitPS < 16:
            pbitPS = 16
    
    # fmtc.resample parameters
    if kernel is None:
        kernel = "bicubic"
        if a1 is None and a2 is None:
            a1 = 0
            a2 = 0.5
    elif not isinstance(kernel, str):
        raise TypeError(funcName + ': \"kernel\" must be a str!')
    
    # Conversion
    if sIsRGB:
        # Skip matrix conversion for RGB input
        # Apply depth conversion for output clip
        clip = Depth(clip, dbitPS, dSType, fulls, fulld, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
    elif sIsGRAY:
        # Apply depth conversion for output clip
        clip = Depth(clip, dbitPS, dSType, fulls, fulld, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
        # Shuffle planes for Gray input
        clip = core.std.ShufflePlanes([clip,clip,clip], [0,0,0], vs.RGB)
        # Set output frame properties
        clip = SetColorSpace(clip, Matrix=0)
    else:
        # Apply chroma up-sampling if needed
        if sHSubS != 1 or sVSubS != 1:
            clip = core.fmtc.resample(clip, kernel=kernel, taps=taps, a1=a1, a2=a2, css="444", planes=[2,3,3], fulls=fulls, fulld=fulls, cplace=cplace, flt=pSType==vs.FLOAT)
        # Apply depth conversion for processed clip
        else:
            clip = Depth(clip, pbitPS, pSType, fulls, fulls, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
        # Apply matrix conversion for YUV or YCoCg input
        if matrix == "OPP":
            clip = core.fmtc.matrix(clip, fulls=fulls, fulld=fulld, coef=[1,1,2/3,0, 1,0,-4/3,0, 1,-1,2/3,0], col_fam=vs.RGB)
            clip = SetColorSpace(clip, Matrix=0)
        elif matrix == "2020cl":
            clip = core.fmtc.matrix2020cl(clip, full=fulls)
        else:
            clip = core.fmtc.matrix(clip, mat=matrix, fulls=fulls, fulld=fulld, col_fam=vs.RGB)
        # Apply depth conversion for output clip
        clip = Depth(clip, dbitPS, dSType, fulld, fulld, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
    
    if compat:
        clip = core.resize.Bicubic(clip, format=vs.COMPATBGR32)
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Main function: ToYUV()
################################################################################################################################
## Convert any color space to YUV/YCoCg with/without sub-sampling.
## If input is RGB, it's assumed to be of full range.
##     Thus, limited range RGB clip should first be manually converted to full range before call this function.
## If matrix is 10, "2020cl" or "bt2020c", the input should be linear RGB.
## It's mainly a wrapper for fmtconv.
## Only constant format is supported, frame properties of the input clip is mostly ignored.
## Note that you may get faster speed with core.resize, or not (for now, dither_type='error_diffusion' is slow).
################################################################################################################################
## Basic parameters
##     input {clip}: clip to be converted
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 16/32 bit float
##     matrix {int|str}: color matrix of output clip
##         decides the conversion coefficients from RGB to YUV
##         check GetMatrix() for available values
##         default: None, guessed according to the color family and size of input clip
##     css {str}: chroma sub-sampling of output clip, similar to the one in fmtc.resample
##         If two number is defined, then the first is horizontal sub-sampling and the second is vertical sub-sampling.
##         For example, "11" is 4:4:4, "21" is 4:2:2, "22" is 4:2:0.
##         preset values:
##         - "444" | "4:4:4" | "11"
##         - "440" | "4:4:0" | "12"
##         - "422" | "4:2:2" | "21"
##         - "420" | "4:2:0" | "22"
##         - "411" | "4:1:1" | "41"
##         - "410" | "4:1:0" | "42"
##         default: 4:4:4 for RGB/Gray input, same as input is for YUV/YCoCg input
##     depth {int}: output bit depth, can be 1-16 bit integer or 16/32 bit float
##         note that 1-7 bit content is still stored as 8 bit integer format
##         default is the same as that of the input clip
##     sample {int}: output sample type, can be 0(vs.INTEGER) or 1(vs.FLOAT)
##         default is the same as that of the input clip
##     full {bool}: define if input/output Gray/YUV/YCoCg clip is of full range
##         default: guessed according to the color family of input clip and "matrix"
################################################################################################################################
## Parameters of depth conversion
##     dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise:
##         same as those in Depth()
################################################################################################################################
## Parameters of resampling
##     kernel, taps, a1, a2, cplace:
##         used for chroma re-sampling, same as those in fmtc.resample
##         default: kernel="bicubic", a1=0, a2=0.5, also known as "Catmull-Rom"
################################################################################################################################
def ToYUV(input, matrix=None, css=None, depth=None, sample=None, full=None, \
dither=None, useZ=None, prefer_props=None, ampo=None, ampn=None, dyn=None, staticnoise=None, \
kernel=None, taps=None, a1=None, a2=None, cplace=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'ToYUV'
    clip = input
    
    if not isinstance(input, vs.VideoNode):
        raise TypeError(funcName + ': \"input\" must be a clip!')
    
    # Get string format parameter "matrix"
    matrix = GetMatrix(input, matrix, False)
    
    # Get properties of input clip
    sFormat = input.format
    
    sColorFamily = sFormat.color_family
    sIsRGB = sColorFamily == vs.RGB
    sIsYUV = sColorFamily == vs.YUV
    sIsGRAY = sColorFamily == vs.GRAY
    sIsYCOCG = sColorFamily == vs.YCOCG
    if sColorFamily == vs.COMPAT:
        raise ValueError(funcName + ': color family *COMPAT* is not supported!')
    
    sbitPS = sFormat.bits_per_sample
    sSType = sFormat.sample_type
    
    sHSubS = 1 << sFormat.subsampling_w
    sVSubS = 1 << sFormat.subsampling_h
    
    if sIsRGB:
        # Always assume full range for RGB input
        fulls = True
    elif full is None:
        # If not set, assume limited range for YUV and Gray input
        # Assume full range for YCgCo and OPP input
        if (sIsGRAY or sIsYUV or sIsYCOCG) and (matrix == "RGB" or matrix == "YCgCo" or matrix == "OPP"):
            fulls = True
        else:
            fulls = False if sIsYUV or sIsGRAY else True
    elif not isinstance(full, int):
        raise TypeError(funcName + ': \"full\" must be a bool!')
    else:
        fulls = full
    
    # Get properties of output clip
    if depth is None:
        dbitPS = sbitPS
    elif not isinstance(depth, int):
        raise TypeError(funcName + ': \"depth\" must be an int!')
    else:
        dbitPS = depth
    if sample is None:
        if depth is None:
            dSType = sSType
        else:
            dSType = vs.FLOAT if dbitPS >= 32 else vs.INTEGER
    elif not isinstance(sample, int):
        raise TypeError(funcName + ': \"sample\" must be an int!')
    elif sample != vs.INTEGER and sample != vs.FLOAT:
        raise ValueError(funcName + ': \"sample\" must be either 0(vs.INTEGER) or 1(vs.FLOAT)!')
    else:
        dSType = sample
    if depth is None and sSType != vs.FLOAT and sample == vs.FLOAT:
        dbitPS = 32
    elif depth is None and sSType != vs.INTEGER and sample == vs.INTEGER:
        dbitPS = 16
    if dSType == vs.INTEGER and (dbitPS < 1 or dbitPS > 16):
        raise ValueError(funcName + ': {0}-bit integer output is not supported!'.format(dbitPS))
    if dSType == vs.FLOAT and (dbitPS != 16 and dbitPS != 32):
        raise ValueError(funcName + ': {0}-bit float output is not supported!'.format(dbitPS))
    
    if full is None:
        # If not set, assume limited range for YUV and Gray output
        # Assume full range for YCgCo and OPP output
        if matrix == "RGB" or matrix == "YCgCo" or matrix == "OPP":
            fulld = True
        else:
            fulld = True if sIsYCOCG else False
    elif not isinstance(full, int):
        raise TypeError(funcName + ': \"full\" must be a bool!')
    else:
        fulld = full
    
    # Chroma sub-sampling parameters
    if css is None:
        dHSubS = sHSubS
        dVSubS = sVSubS
        css = '{ssw}{ssh}'.format(ssw=dHSubS, ssh=dVSubS)
    elif not isinstance(css, str):
        raise TypeError(funcName + ': \"css\" must be a str!')
    else:
        if css == "444" or css == "4:4:4":
            css = "11"
        elif css == "440" or css == "4:4:0":
            css = "12"
        elif css == "422" or css == "4:2:2":
            css = "21"
        elif css == "420" or css == "4:2:0":
            css = "22"
        elif css == "411" or css == "4:1:1":
            css = "41"
        elif css == "410" or css == "4:1:0":
            css = "42"
        dHSubS = int(css[0])
        dVSubS = int(css[1])
    
    # Get properties of internal processed clip
    pSType = max(sSType, dSType) # If float sample type is involved, then use float for conversion
    if pSType == vs.FLOAT:
        # For float sample type, only 32-bit is supported by fmtconv
        pbitPS = 32
    else:
        # Apply conversion in the higher one of input and output bit depth
        pbitPS = max(sbitPS, dbitPS)
        # For integer sample type, only 8-, 9-, 10-, 12-, 16-bit is supported by fmtc.matrix
        if pbitPS == 11:
            pbitPS = 12
        elif pbitPS > 12 and pbitPS < 16:
            pbitPS = 16
        if dHSubS != sHSubS or dVSubS != sVSubS:
            # When chroma re-sampling is needed, always process in 16-bit for integer sample type
            pbitPS = 16
    
    # fmtc.resample parameters
    if kernel is None:
        kernel = "bicubic"
        if a1 is None and a2 is None:
            a1 = 0
            a2 = 0.5
    elif not isinstance(kernel, str):
        raise TypeError(funcName + ': \"kernel\" must be a str!')
    
    # Conversion
    if sIsYUV or sIsYCOCG:
        # Skip matrix conversion for YUV/YCoCg input
        # Change chroma sub-sampling if needed
        if dHSubS != sHSubS or dVSubS != sVSubS:
            # Apply depth conversion for processed clip
            clip = Depth(clip, pbitPS, pSType, fulls, fulls, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
            clip = core.fmtc.resample(clip, kernel=kernel, taps=taps, a1=a1, a2=a2, css=css, planes=[2,3,3], fulls=fulls, fulld=fulls, cplace=cplace)
        # Apply depth conversion for output clip
        clip = Depth(clip, dbitPS, dSType, fulls, fulld, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
    elif sIsGRAY:
        # Apply depth conversion for output clip
        clip = Depth(clip, dbitPS, dSType, fulls, fulld, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
        # Shuffle planes for Gray input
        widthc = input.width // dHSubS
        heightc = input.height // dVSubS
        UV = core.std.BlankClip(clip, width=widthc, height=heightc, \
        color=1 << (dbitPS - 1) if dSType == vs.INTEGER else 0)
        clip = core.std.ShufflePlanes([clip,UV,UV], [0,0,0], vs.YUV)
    else:
        # Apply depth conversion for processed clip
        clip = Depth(clip, pbitPS, pSType, fulls, fulls, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
        # Apply matrix conversion for RGB input
        if matrix == "OPP":
            clip = core.fmtc.matrix(clip, fulls=fulls, fulld=fulld, coef=[1/3,1/3,1/3,0, 1/2,0,-1/2,0, 1/4,-1/2,1/4,0], col_fam=vs.YUV)
            clip = SetColorSpace(clip, Matrix=2)
        elif matrix == "2020cl":
            clip = core.fmtc.matrix2020cl(clip, full=fulld)
        else:
            clip = core.fmtc.matrix(clip, mat=matrix, fulls=fulls, fulld=fulld, col_fam=vs.YCOCG if matrix == "YCgCo" else vs.YUV)
        # Change chroma sub-sampling if needed
        if dHSubS != sHSubS or dVSubS != sVSubS:
            clip = core.fmtc.resample(clip, kernel=kernel, taps=taps, a1=a1, a2=a2, css=css, planes=[2,3,3], fulls=fulld, fulld=fulld, cplace=cplace)
        # Apply depth conversion for output clip
        clip = Depth(clip, dbitPS, dSType, fulld, fulld, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Main function: BM3D()
################################################################################################################################
## A wrap function for BM3D/V-BM3D denoising filter.
## The BM3D filtering is always done in 16-bit int or 32-bit float opponent(OPP) color space internally.
## It can automatically convert any input color space to OPP and convert it back after filtering.
## Alternatively, you can specify "output" to force outputting RGB or OPP, and "css" to change chroma subsampling.
## For Gray input, no color space conversion is involved, thus "output" and "css" won't take effect.
## You can specify "refine" for any number of final estimate refinements.
################################################################################################################################
## Basic parameters
##     input {clip}: clip to be filtered
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 16/32 bit float
##     sigma {float[]}: same as "sigma" in BM3D, used for both basic estimate and final estimate
##         the strength of filtering, should be carefully adjusted according to the noise
##         set 0 to disable the filtering of corresponding plane
##         default: [5.0,5.0,5.0]
##     radius1 {int}: temporal radius of basic estimate
##         - 0: BM3D (spatial denoising)
##         - 1~16: temporal radius of V-BM3D (spatial-temporal denoising)
##         default: 0
##     radius2 {int}: temporal radius of final estimate
##         default is the same as "radius1"
##     profile1 {str}: same as "profile" in BM3D basic estimate
##         default: "fast"
##     profile2 {str}: same as "profile" in BM3D final estimate
##         default is the same as "profile1"
################################################################################################################################
## Advanced parameters
##     refine {int}: refinement times
##         - 0: basic estimate only
##         - 1: basic estimate + refined with final estimate, the original behavior of BM3D
##         - n: basic estimate + refined with final estimate for n times
##             each final estimate takes the filtered clip in previous estimate as reference clip to filter the input clip
##         default: 1
##     pre {clip} (optional): pre-filtered clip for basic estimate
##         must be of the same format and dimension as the input clip
##         should be a clip better suited for block-matching than the input clip
##     ref {clip} (optional): basic estimate clip
##         must be of the same format and dimension as the input clip
##         replace the basic estimate of BM3D and serve as the reference clip for final estimate
##     psample {int}: internal processed precision
##         - 0: 16-bit integer, less accuracy, less memory consumption
##         - 1: 32-bit float, more accuracy, more memory consumption
##         default: 1
################################################################################################################################
## Parameters of input properties
##     matrix {int|str}: color matrix of input clip, only makes sense for YUV/YCoCg input
##         check GetMatrix() for available values
##         default: guessed according to the color family and size of input clip
##     full {bool}: define if input clip is of full range
##         default: guessed according to the color family of input clip and "matrix"
################################################################################################################################
## Parameters of output properties
##     output {int}: type of output clip, doesn't make sense for Gray input
##         - 0: same format as input clip
##         - 1: full range RGB (converted from input clip)
##         - 2: full range OPP (converted from full range RGB, the color space where the filtering takes place)
##         default: 0
##     css {str}: chroma subsampling of output clip, only valid when output=0 and input clip is YUV/YCoCg
##         check ToYUV() for available values
##         default is the same as that of the input clip
##     depth {int}: bit depth of output clip, can be 1-16 for integer or 16/32 for float
##         note that 1-7 bit content is still stored as 8 bit integer format
##         default is the same as that of the input clip
##     sample {int}: sample type of output clip, can be 0(vs.INTEGER) or 1(vs.FLOAT)
##         default is the same as that of the input clip
################################################################################################################################
## Parameters of depth conversion
##     dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise:
##         same as those in Depth()
################################################################################################################################
## Parameters of resampling
##     cu_kernel, cu_taps, cu_a1, cu_a2, cu_cplace:
##         used for chroma up-sampling, same as those in fmtc.resample
##         default: kernel="bicubic", a1=0, a2=0.5, also known as "Catmull-Rom"
##     cd_kernel, cd_taps, cd_a1, cd_a2, cd_cplace:
##         used for chroma down-sampling, same as those in fmtc.resample
##         default: kernel="bicubic", a1=0, a2=0.5, also known as "Catmull-Rom"
################################################################################################################################
## Parameters of BM3D basic estimate
##     block_size1, block_step1, group_size1, bm_range1, bm_step1, ps_num1, ps_range1, ps_step1, th_mse1, hard_thr:
##         same as those in bm3d.Basic/bm3d.VBasic
################################################################################################################################
## Parameters of BM3D final estimate
##     block_size2, block_step2, group_size2, bm_range2, bm_step2, ps_num2, ps_range2, ps_step2, th_mse2:
##         same as those in bm3d.Final/bm3d.VFinal
################################################################################################################################
def BM3D(input, sigma=None, radius1=None, radius2=None, profile1=None, profile2=None, \
refine=None, pre=None, ref=None, psample=None, \
matrix=None, full=None, \
output=None, css=None, depth=None, sample=None, \
dither=None, useZ=None, prefer_props=None, ampo=None, ampn=None, dyn=None, staticnoise=None, \
cu_kernel=None, cu_taps=None, cu_a1=None, cu_a2=None, cu_cplace=None, \
cd_kernel=None, cd_taps=None, cd_a1=None, cd_a2=None, cd_cplace=None, \
block_size1=None, block_step1=None, group_size1=None, bm_range1=None, bm_step1=None, ps_num1=None, ps_range1=None, ps_step1=None, th_mse1=None, hard_thr=None, \
block_size2=None, block_step2=None, group_size2=None, bm_range2=None, bm_step2=None, ps_num2=None, ps_range2=None, ps_step2=None, th_mse2=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'BM3D'
    clip = input
    
    if not isinstance(input, vs.VideoNode):
        raise TypeError(funcName + ': \"input\" must be a clip!')
    
    # Get string format parameter "matrix"
    matrix = GetMatrix(input, matrix, True)
    
    # Get properties of input clip
    sFormat = input.format
    
    sColorFamily = sFormat.color_family
    sIsRGB = sColorFamily == vs.RGB
    sIsYUV = sColorFamily == vs.YUV
    sIsGRAY = sColorFamily == vs.GRAY
    sIsYCOCG = sColorFamily == vs.YCOCG
    if sColorFamily == vs.COMPAT:
        raise ValueError(funcName + ': color family *COMPAT* is not supported!')
    
    sbitPS = sFormat.bits_per_sample
    sSType = sFormat.sample_type
    
    sHSubS = 1 << sFormat.subsampling_w
    sVSubS = 1 << sFormat.subsampling_h
    
    if full is None:
        # If not set, assume limited range for YUV and Gray input
        # Assume full range for YCgCo and OPP input
        if (sIsGRAY or sIsYUV or sIsYCOCG) and (matrix == "RGB" or matrix == "YCgCo" or matrix == "OPP"):
            fulls = True
        else:
            fulls = False if sIsYUV or sIsGRAY else True
    elif not isinstance(full, int):
        raise TypeError(funcName + ': \"full\" must be a bool!')
    else:
        fulls = full
    
    # Get properties of internal processed clip
    if psample is None:
        psample = vs.FLOAT
    elif not isinstance(psample, int):
        raise TypeError(funcName + ': \"psample\" must be an int!')
    elif psample != vs.INTEGER and psample != vs.FLOAT:
        raise ValueError(funcName + ': \"psample\" must be either 0(vs.INTEGER) or 1(vs.FLOAT)!')
    pbitPS = 16 if psample == vs.INTEGER else 32
    pSType = psample
    
    # Chroma sub-sampling parameters
    if css is None:
        dHSubS = sHSubS
        dVSubS = sVSubS
        css = '{ssw}{ssh}'.format(ssw=dHSubS, ssh=dVSubS)
    elif not isinstance(css, str):
        raise TypeError(funcName + ': \"css\" must be a str!')
    else:
        if css == "444" or css == "4:4:4":
            css = "11"
        elif css == "440" or css == "4:4:0":
            css = "12"
        elif css == "422" or css == "4:2:2":
            css = "21"
        elif css == "420" or css == "4:2:0":
            css = "22"
        elif css == "411" or css == "4:1:1":
            css = "41"
        elif css == "410" or css == "4:1:0":
            css = "42"
        dHSubS = int(css[0])
        dVSubS = int(css[1])
    
    if cu_cplace is not None and cd_cplace is None:
        cd_cplace = cu_cplace
    
    # Parameters processing
    if sigma is None:
        sigma = [5.0,5.0,5.0]
    else:
        if isinstance(sigma, int):
            sigma = float(sigma)
            sigma = [sigma,sigma,sigma]
        elif isinstance(sigma, float):
            sigma = [sigma,sigma,sigma]
        elif isinstance(sigma, list):
            while len(sigma) < 3:
                sigma.append(sigma[len(sigma) - 1])
        else:
            raise TypeError(funcName + ': sigma must be a float[] or an int[]!')
    if sIsGRAY:
        sigma = [sigma[0],0,0]
    skip = sigma[0] <= 0 and sigma[1] <= 0 and sigma[2] <= 0
    
    if radius1 is None:
        radius1 = 0
    elif not isinstance(radius1, int):
        raise TypeError(funcName + ': \"radius1\" must be an int!')
    elif radius1 < 0:
        raise ValueError(funcName + ': valid range of \"radius1\" is [0, +inf)!')
    if radius2 is None:
        radius2 = radius1
    elif not isinstance(radius2, int):
        raise TypeError(funcName + ': \"radius2\" must be an int!')
    elif radius2 < 0:
        raise ValueError(funcName + ': valid range of \"radius2\" is [0, +inf)!')
    
    if profile1 is None:
        profile1 = "fast"
    elif not isinstance(profile1, str):
        raise TypeError(funcName + ': \"profile1\" must be a str!')
    if profile2 is None:
        profile2 = profile1
    elif not isinstance(profile2, str):
        raise TypeError(funcName + ': \"profile2\" must be a str!')
    
    if refine is None:
        refine = 1
    elif not isinstance(refine, int):
        raise TypeError(funcName + ': \"refine\" must be an int!')
    elif refine < 0:
        raise ValueError(funcName + ': valid range of \"refine\" is [0, +inf)!')
    
    if output is None:
        output = 0
    elif not isinstance(output, int):
        raise TypeError(funcName + ': \"output\" must be an int!')
    elif output < 0 or output > 2:
        raise ValueError(funcName + ': valid values of \"output\" are 0, 1 and 2!')
    
    if pre is not None:
        if not isinstance(pre, vs.VideoNode):
            raise TypeError(funcName + ': \"pre\" must be a clip!')
        if pre.format.id != sFormat.id:
            raise ValueError(funcName + ': clip \"pre\" must be of the same format as the input clip!')
        if pre.width != input.width or pre.height != input.height:
            raise ValueError(funcName + ': clip \"pre\" must be of the same size as the input clip!')
    
    if ref is not None:
        if not isinstance(ref, vs.VideoNode):
            raise TypeError(funcName + ': \"ref\" must be a clip!')
        if ref.format.id != sFormat.id:
            raise ValueError(funcName + ': clip \"ref\" must be of the same format as the input clip!')
        if ref.width != input.width or ref.height != input.height:
            raise ValueError(funcName + ': clip \"ref\" must be of the same size as the input clip!')
    
    # Get properties of output clip
    if depth is None:
        if output == 0:
            dbitPS = sbitPS
        else:
            dbitPS = pbitPS
    elif not isinstance(depth, int):
        raise TypeError(funcName + ': \"depth\" must be an int!')
    else:
        dbitPS = depth
    if sample is None:
        if depth is None:
            if output == 0:
                dSType = sSType
            else:
                dSType = pSType
        else:
            dSType = vs.FLOAT if dbitPS >= 32 else vs.INTEGER
    elif not isinstance(sample, int):
        raise TypeError(funcName + ': \"sample\" must be an int!')
    elif sample != vs.INTEGER and sample != vs.FLOAT:
        raise ValueError(funcName + ': \"sample\" must be either 0(vs.INTEGER) or 1(vs.FLOAT)!')
    else:
        dSType = sample
    if depth is None and sSType != vs.FLOAT and sample == vs.FLOAT:
        dbitPS = 32
    elif depth is None and sSType != vs.INTEGER and sample == vs.INTEGER:
        dbitPS = 16
    if dSType == vs.INTEGER and (dbitPS < 1 or dbitPS > 16):
        raise ValueError(funcName + ': {0}-bit integer output is not supported!'.format(dbitPS))
    if dSType == vs.FLOAT and (dbitPS != 16 and dbitPS != 32):
        raise ValueError(funcName + ': {0}-bit float output is not supported!'.format(dbitPS))
    
    if output == 0:
        fulld = fulls
    else:
        # Always full range output when output=1|output=2 (full range RGB or full range OPP)
        fulld = True
    
    # Convert to processed format
    # YUV/YCoCg/RGB input is converted to opponent color space as full range YUV
    # Gray input is converted to full range Gray
    onlyY = False
    if sIsGRAY:
        onlyY = True
        # Convert Gray input to full range Gray in processed format
        clip = Depth(clip, pbitPS, pSType, fulls, True, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
        if pre is not None:
            pre = Depth(pre, pbitPS, pSType, fulls, True, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
        if ref is not None:
            ref = Depth(ref, pbitPS, pSType, fulls, True, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
    else:
        # Convert input to full range RGB
        clip = ToRGB(clip, matrix, pbitPS, pSType, fulls, \
        dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise, cu_kernel, cu_taps, cu_a1, cu_a2, cu_cplace, False)
        if pre is not None:
            pre = ToRGB(pre, matrix, pbitPS, pSType, fulls, \
            dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise, cu_kernel, cu_taps, cu_a1, cu_a2, cu_cplace, False)
        if ref is not None:
            ref = ToRGB(ref, matrix, pbitPS, pSType, fulls, \
            dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise, cu_kernel, cu_taps, cu_a1, cu_a2, cu_cplace, False)
        # Convert full range RGB to full range OPP
        clip = ToYUV(clip, "OPP", "444", pbitPS, pSType, True, \
        dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise, cu_kernel, cu_taps, cu_a1, cu_a2, cu_cplace)
        if pre is not None:
            pre = ToYUV(pre, "OPP", "444", pbitPS, pSType, True, \
            dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise, cu_kernel, cu_taps, cu_a1, cu_a2, cu_cplace)
        if ref is not None:
            ref = ToYUV(ref, "OPP", "444", pbitPS, pSType, True, \
            dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise, cu_kernel, cu_taps, cu_a1, cu_a2, cu_cplace)
        # Convert OPP to Gray if only Y is processed
        srcOPP = clip
        if sigma[1] <= 0 and sigma[2] <= 0:
            onlyY = True
            clip = core.std.ShufflePlanes([clip], [0], vs.GRAY)
            if pre is not None:
                pre = core.std.ShufflePlanes([pre], [0], vs.GRAY)
            if ref is not None:
                ref = core.std.ShufflePlanes([ref], [0], vs.GRAY)
    
    # Basic estimate
    if ref is not None:
        # Use custom basic estimate specified by clip "ref"
        flt = ref
    elif skip:
        flt = clip
    elif radius1 < 1:
        # Apply BM3D basic estimate
        # Optional pre-filtered clip for block-matching can be specified by "pre"
        flt = core.bm3d.Basic(clip, ref=pre, profile=profile1, sigma=sigma, \
        block_size=block_size1, block_step=block_step1, group_size=group_size1, \
        bm_range=bm_range1, bm_step=bm_step1, th_mse=th_mse1, hard_thr=hard_thr, matrix=100)
    else:
        # Apply V-BM3D basic estimate
        # Optional pre-filtered clip for block-matching can be specified by "pre"
        flt = core.bm3d.VBasic(clip, ref=pre, profile=profile1, sigma=sigma, radius=radius1, \
        block_size=block_size1, block_step=block_step1, group_size=group_size1, \
        bm_range=bm_range1, bm_step=bm_step1, ps_num=ps_num1, ps_range=ps_range1, ps_step=ps_step1, \
        th_mse=th_mse1, hard_thr=hard_thr, matrix=100).bm3d.VAggregate(radius=radius1, sample=pSType)
        # Shuffle Y plane back if not processed
        if not onlyY and sigma[0] <= 0:
            flt = core.std.ShufflePlanes([clip,flt,flt], [0,1,2], vs.YUV)
    
    # Final estimate
    for i in range(0, refine):
        if skip:
            flt = clip
        elif radius2 < 1:
            # Apply BM3D final estimate
            flt = core.bm3d.Final(clip, ref=flt, profile=profile2, sigma=sigma, \
            block_size=block_size2, block_step=block_step2, group_size=group_size2, \
            bm_range=bm_range2, bm_step=bm_step2, th_mse=th_mse2, matrix=100)
        else:
            # Apply V-BM3D final estimate
            flt = core.bm3d.VFinal(clip, ref=flt, profile=profile2, sigma=sigma, radius=radius2, \
            block_size=block_size2, block_step=block_step2, group_size=group_size2, \
            bm_range=bm_range2, bm_step=bm_step2, ps_num=ps_num2, ps_range=ps_range2, ps_step=ps_step2, \
            th_mse=th_mse2, matrix=100).bm3d.VAggregate(radius=radius2, sample=pSType)
            # Shuffle Y plane back if not processed
            if not onlyY and sigma[0] <= 0:
                flt = core.std.ShufflePlanes([clip,flt,flt], [0,1,2], vs.YUV)
    
    # Convert to output format
    if sIsGRAY:
        clip = Depth(flt, dbitPS, dSType, True, fulld, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
    else:
        # Shuffle back to YUV if not all planes are processed
        if onlyY:
            clip = core.std.ShufflePlanes([flt,srcOPP,srcOPP], [0,1,2], vs.YUV)
        elif sigma[1] <= 0 or sigma[2] <= 0:
            clip = core.std.ShufflePlanes([flt, clip if sigma[1] <= 0 else flt, \
            clip if sigma[2] <= 0 else flt], [0,1,2], vs.YUV)
        else:
            clip = flt
        # Convert to final output format
        if output <= 1:
            # Convert full range OPP to full range RGB
            clip = ToRGB(clip, "OPP", pbitPS, pSType, True, \
            dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise, cu_kernel, cu_taps, cu_a1, cu_a2, cu_cplace, False)
        if output <= 0 and not sIsRGB:
            # Convert full range RGB to YUV/YCoCg
            clip = ToYUV(clip, matrix, css, dbitPS, dSType, fulld, \
            dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise, cd_kernel, cd_taps, cd_a1, cd_a2, cd_cplace)
        else:
            # Depth conversion for RGB or OPP output
            clip = Depth(clip, dbitPS, dSType, True, fulld, dither, useZ, prefer_props, ampo, ampn, dyn, staticnoise)
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Main function: VFRSplice()
################################################################################################################################
## Splice multiple clips with different frame rate, and output timecode file.
## Each input clip is CFR(constant frame rate).
## The output clip is VFR(variational frame rate).
################################################################################################################################
## Basic parameters
##     clips {clip}: any number of clips to splice
##         each clip should be CFR
##     tcfile {str}: timecode file output
##         default: None
##     v2 {bool}: timecode format
##         True for v2 output and False for v1 output
##         default: True
##     precision {int}: precision of time and frame rate
##         a decimal number indicating how many digits should be displayed after the decimal point for a fixed-point value
##         default: 6
################################################################################################################################
def VFRSplice(clips, tcfile=None, v2=None, precision=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'VFRSplice'
    
    # Arguments
    if isinstance(clips, vs.VideoNode):
        clips = [clips]
    elif isinstance(clips, list):
        for clip in clips:
            if not isinstance(clip, vs.VideoNode):
                raise TypeError(funcName + ': each element in \"clips\" must be a clip!')
            if clip.fps_num == 0 or clip.fps_den == 0:
                raise ValueError(funcName + ': each clip in \"clips\" must be CFR!')
    else:
        raise TypeError(funcName + ': \"clips\" must be a clip or a list of clips!')
    
    if tcfile is not None and not isinstance(tcfile, str):
        raise TypeError(funcName + ': \"tcfile\" must be a str!')
    
    if v2 is None:
        v2 = True
    elif not isinstance(v2, int):
        raise TypeError(funcName + ': \"v2\" must be a bool!')
    
    if precision is None:
        precision = 6
    elif not isinstance(precision, int):
        raise TypeError(funcName + ': \"precision\" must be an int!')
    
    # Fraction to str function
    def frac2str(num, den, precision=6):
        return '{:<.{precision}F}'.format(num / den, precision=precision)
    
    # Timecode file
    if tcfile is None:
        pass
    else:
        # Get timecode v1 list
        cur_frame = 0
        tc_list = []
        index = 0
        for clip in clips:
            if index > 0 and clip.fps_num == tc_list[index - 1][2] and clip.fps_den == tc_list[index - 1][3]:
                tc_list[index - 1] = (tc_list[index - 1][0], cur_frame + clip.num_frames - 1, clip.fps_num, clip.fps_den)
            else:
                tc_list.append((cur_frame, cur_frame + clip.num_frames - 1, clip.fps_num, clip.fps_den))
            cur_frame += clip.num_frames
            index += 1
        
        # Write to timecode file
        ofile = open(tcfile, 'w')
        if v2: # timecode v2
            olines = ['# timecode format v2\n']
            frames = tc_list[len(tc_list) - 1][1] + 1
            time = 0 # ms
            for tc in tc_list:
                frame_duration = 1000 * tc[3] / tc[2] # ms
                for frame in range(tc[0], tc[1] + 1):
                    olines.append('{:<.{precision}F}\n'.format(time, precision=precision))
                    time += frame_duration
        else: # timecode v1
            olines = ['# timecode format v1\n', 'Assume {}\n'.format(frac2str(tc_list[0][2], tc_list[0][3], precision))]
            for tc in tc_list:
                olines.append('{},{},{}\n'.format(tc[0], tc[1], frac2str(tc[2], tc[3], precision)))
        try:
            ofile.writelines(olines)
        finally:
            ofile.close()
    
    # Output spliced clip
    return core.std.Splice(clips, mismatch=True)
################################################################################################################################





################################################################################################################################
################################################################################################################################
################################################################################################################################
## Runtime functions below
################################################################################################################################
################################################################################################################################
################################################################################################################################


################################################################################################################################
## Runtime function: PlaneStatistics()
################################################################################################################################
## Calculate statistics of specific plane and store them as frame properties
## All the values are normalized (float that the peak-to-peak value is 1)
## Supported statistics:
##     mean: stored as frame property 'PlaneMean'
##     mean absolute deviation: stored as frame property 'PlaneMAD'
##     variance: stored as frame property 'PlaneVar'
##     standard deviation: stored as frame property 'PlaneSTD'
##     root mean square: stored as frame property 'PlaneRMS'
################################################################################################################################
## Basic parameters
##     clip {clip}: clip to be evaluated
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 32 bit float
##     plane {int}: specify which plane to evaluate
##         default: 0
##     mean {bool}: whether to calculate mean
##         default: True
##     mad {bool}: whether to calculate mean absolute deviation
##         default: True
##     var {bool}: whether to calculate variance
##         default: True
##     std {bool}: whether to calculate standard deviation
##         default: True
##     rms {bool}: whether to calculate root mean square
##         default: True
################################################################################################################################
def PlaneStatistics(clip, plane=None, mean=True, mad=True, var=True, std=True, rms=True):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'PlaneStatistics'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Get properties of input clip
    sFormat = clip.format
    
    sSType = sFormat.sample_type
    sbitPS = sFormat.bits_per_sample
    sNumPlanes = sFormat.num_planes
    
    valueRange = (1 << sbitPS) - 1 if sSType == vs.INTEGER else 1
    
    # Parameters
    if plane is None:
        plane = 0
    elif not isinstance(plane, int):
        raise TypeError(funcName + ': \"plane\" must be an int!')
    elif plane < 0 or plane > sNumPlanes:
        raise ValueError(funcName + ': valid range of \"plane\" is [0, sNumPlanes)!'.format(sNumPlanes=sNumPlanes))
    
    floatFormat = core.register_format(vs.GRAY, vs.FLOAT, 32, 0, 0)
    floatBlk = core.std.BlankClip(clip, format=floatFormat.id)
    
    clipPlane = GetPlane(clip, plane)
    
    # Plane Mean
    clip = PlaneAverage(clip, plane, "PlaneMean")
    
    # Plane MAD (mean absolute deviation)
    if mad:
        '''# always float precision
        def _PlaneADFrame(n, f, clip):
            mean = f.props.PlaneMean
            expr = "x {gain} * {mean} - abs".format(gain=1 / valueRange, mean=mean)
            return core.std.Expr(clip, expr, floatFormat.id)
        ADclip = core.std.FrameEval(floatBlk, functools.partial(_PlaneADFrame, clip=clipPlane), clip)'''
        def _PlaneADFrame(n, f, clip):
            mean = f.props.PlaneMean * valueRange
            expr = "x {mean} - abs".format(mean=mean)
            return core.std.Expr(clip, expr)
        ADclip = core.std.FrameEval(clipPlane, functools.partial(_PlaneADFrame, clip=clipPlane), clip)
        ADclip = PlaneAverage(ADclip, 0, "PlaneMAD")
        
        def _PlaneMADTransfer(n, f):
            fout = f[0].copy()
            fout.props.PlaneMAD = f[1].props.PlaneMAD
            return fout
        clip = core.std.ModifyFrame(clip, [clip, ADclip], selector=_PlaneMADTransfer)
    
    # Plane Var (variance) and STD (standard deviation)
    if var or std:
        def _PlaneSDFrame(n, f, clip):
            mean = f.props.PlaneMean * valueRange
            expr = "x {mean} - dup *".format(mean=mean)
            return core.std.Expr(clip, expr, floatFormat.id)
        SDclip = core.std.FrameEval(floatBlk, functools.partial(_PlaneSDFrame, clip=clipPlane), clip)
        SDclip = PlaneAverage(SDclip, 0, "PlaneVar")
        
        def _PlaneVarSTDTransfer(n, f):
            fout = f[0].copy()
            if var:
                fout.props.PlaneVar = f[1].props.PlaneVar / (valueRange * valueRange)
            if std:
                fout.props.PlaneSTD = math.sqrt(f[1].props.PlaneVar) / valueRange
            return fout
        clip = core.std.ModifyFrame(clip, [clip, SDclip], selector=_PlaneVarSTDTransfer)
    
    # Plane RMS (root mean square)
    if rms:
        expr = "x x *"
        squareClip = core.std.Expr(clipPlane, expr, floatFormat.id)
        squareClip = PlaneAverage(squareClip, 0, "PlaneMS")
        
        def _PlaneRMSTransfer(n, f):
            fout = f[0].copy()
            fout.props.PlaneRMS = math.sqrt(f[1].props.PlaneMS) / valueRange
            return fout
        clip = core.std.ModifyFrame(clip, [clip, squareClip], selector=_PlaneRMSTransfer)
    
    # Delete frame property "PlaneMean" if not needed
    if not mean:
        clip = core.std.SetFrameProp(clip, "PlaneMean", delete=True)
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Runtime function: PlaneCompare()
################################################################################################################################
## Compare specific plane of 2 clips and store the statistical results as frame properties
## All the values are normalized (float of which the peak-to-peak value is 1) except PSNR
## Supported statistics:
##     mean absolute error: stored as frame property 'PlaneMAE', aka L1 distance
##     root of mean squared error: stored as frame property 'PlaneRMSE', aka L2 distance(Euclidean distance)
##     peak signal-to-noise ratio: stored as frame property 'PlanePSNR', maximum value is 100.0 (when 2 planes are identical)
##     covariance: stored as frame property 'PlaneCov'
##     correlation: stored as frame property 'PlaneCorr'
################################################################################################################################
## Basic parameters
##     clip1 {clip}: the first clip to be evaluated, will be copied to output
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 32 bit float
##     clip2 {clip}: the second clip, to be compared with the first one
##         must be of the same format and dimension as the "clip1"
##     plane {int}: specify which plane to evaluate
##         default: 0
##     mae {bool}: whether to calculate mean absolute error
##         default: True
##     rmse {bool}: whether to calculate root of mean squared error
##         default: True
##     psnr {bool}: whether to calculate peak signal-to-noise ratio
##         default: True
##     cov {bool}: whether to calculate covariance
##         default: True
##     corr {bool}: whether to calculate correlation
##         default: True
################################################################################################################################
def PlaneCompare(clip1, clip2, plane=None, mae=True, rmse=True, psnr=True, cov=True, corr=True):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'PlaneCompare'
    
    if not isinstance(clip1, vs.VideoNode):
        raise TypeError(funcName + ': \"clip1\" must be a clip!')
    if not isinstance(clip2, vs.VideoNode):
        raise TypeError(funcName + ': \"clip2\" must be a clip!')
    
    # Get properties of input clip
    sFormat = clip1.format
    if sFormat.id != clip2.format.id:
        raise ValueError(funcName + ': \"clip1\" and \"clip2\" must be of the same format!')
    if clip1.width != clip2.width or clip1.height != clip2.height:
        raise ValueError(funcName + ': \"clip1\" and \"clip2\" must be of the same width and height!')
    
    sSType = sFormat.sample_type
    sbitPS = sFormat.bits_per_sample
    sNumPlanes = sFormat.num_planes
    
    valueRange = (1 << sbitPS) - 1 if sSType == vs.INTEGER else 1
    
    # Parameters
    if plane is None:
        plane = 0
    elif not isinstance(plane, int):
        raise TypeError(funcName + ': \"plane\" must be an int!')
    elif plane < 0 or plane > sNumPlanes:
        raise ValueError(funcName + ': valid range of \"plane\" is [0, sNumPlanes)!'.format(sNumPlanes=sNumPlanes))
    
    floatFormat = core.register_format(vs.GRAY, vs.FLOAT, 32, 0, 0)
    floatBlk = core.std.BlankClip(clip1, format=floatFormat.id)
    
    clip1Plane = GetPlane(clip1, plane)
    clip2Plane = GetPlane(clip2, plane)
    
    # Plane MAE (mean absolute error)
    if mae:
        expr = "x y - abs"
        ADclip = core.std.Expr([clip1Plane, clip2Plane], expr, floatFormat.id)
        ADclip = PlaneAverage(ADclip, 0, "PlaneMAE")
        
        def _PlaneMAETransfer(n, f):
            fout = f[0].copy()
            fout.props.PlaneMAE = f[1].props.PlaneMAE / valueRange
            return fout
        clip1 = core.std.ModifyFrame(clip1, [clip1, ADclip], selector=_PlaneMAETransfer)
    
    # Plane RMSE (root of mean squared error) and PSNR (peak signal-to-noise ratio)
    if rmse or psnr:
        expr = "x y - dup *"
        SDclip = core.std.Expr([clip1Plane, clip2Plane], expr, floatFormat.id)
        SDclip = PlaneAverage(SDclip, 0, "PlaneMSE")
        
        def _PlaneRMSEnPSNRTransfer(n, f):
            fout = f[0].copy()
            if rmse:
                fout.props.PlaneRMSE = math.sqrt(f[1].props.PlaneMSE) / valueRange
            if psnr:
                fout.props.PlanePSNR = min(10 * math.log(valueRange * valueRange / f[1].props.PlaneMSE, 10), 99.99999999999999) if f[1].props.PlaneMSE > 0 else 100.0
            return fout
        clip1 = core.std.ModifyFrame(clip1, [clip1, SDclip], selector=_PlaneRMSEnPSNRTransfer)
    
    # Plane Cov (covariance) and Corr (correlation)
    if cov or corr:
        clip1Mean = PlaneAverage(clip1Plane, 0, "PlaneMean")
        clip2Mean = PlaneAverage(clip2Plane, 0, "PlaneMean")
        
        def _PlaneCoDFrame(n, f, clip1, clip2):
            mean1 = f[0].props.PlaneMean * valueRange
            mean2 = f[1].props.PlaneMean * valueRange
            expr = "x {mean1} - y {mean2} - *".format(mean1=mean1, mean2=mean2)
            return core.std.Expr([clip1, clip2], expr, floatFormat.id)
        CoDclip = core.std.FrameEval(floatBlk, functools.partial(_PlaneCoDFrame, clip1=clip1Plane, clip2=clip2Plane), [clip1Mean, clip2Mean])
        CoDclip = PlaneAverage(CoDclip, 0, "PlaneCov")
        clips = [clip1, CoDclip]
        
        if corr:
            def _PlaneSDFrame(n, f, clip):
                mean = f.props.PlaneMean * valueRange
                expr = "x {mean} - dup *".format(mean=mean)
                return core.std.Expr(clip, expr, floatFormat.id)
            SDclip1 = core.std.FrameEval(floatBlk, functools.partial(_PlaneSDFrame, clip=clip1Plane), clip1Mean)
            SDclip2 = core.std.FrameEval(floatBlk, functools.partial(_PlaneSDFrame, clip=clip2Plane), clip2Mean)
            SDclip1 = PlaneAverage(SDclip1, 0, "PlaneVar")
            SDclip2 = PlaneAverage(SDclip2, 0, "PlaneVar")
            clips.append(SDclip1)
            clips.append(SDclip2)
        
        def _PlaneCovTransfer(n, f):
            fout = f[0].copy()
            if cov:
                fout.props.PlaneCov = f[1].props.PlaneCov / (valueRange * valueRange)
            if corr:
                std1std2 = math.sqrt(f[2].props.PlaneVar * f[3].props.PlaneVar)
                fout.props.PlaneCorr = f[1].props.PlaneCov / std1std2 if std1std2 > 0 else float("inf")
            return fout
        clip1 = core.std.ModifyFrame(clip1, clips, selector=_PlaneCovTransfer)
    
    # Output
    return clip1
################################################################################################################################


################################################################################################################################
## Runtime function: ShowAverage()
################################################################################################################################
## Display unnormalized average of each plane
################################################################################################################################
## Basic parameters
##     clip {clip}: clip to be evaluated
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 16/32 bit float
################################################################################################################################
## Advanced parameters
##     alignment {int}: same as the one in text.Text()
##         default: 7
################################################################################################################################
def ShowAverage(clip, alignment=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'ShowAverage'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Get properties of input clip
    sFormat = clip.format
    
    sColorFamily = sFormat.color_family
    sIsYUV = sColorFamily == vs.YUV
    sIsYCOCG = sColorFamily == vs.YCOCG
    
    sSType = sFormat.sample_type
    sbitPS = sFormat.bits_per_sample
    sNumPlanes = sFormat.num_planes
    
    valueRange = (1 << sbitPS) - 1 if sSType == vs.INTEGER else 1
    offset = [0, -0.5, -0.5] if sSType == vs.FLOAT and (sIsYUV or sIsYCOCG) else [0, 0, 0]
    
    # Process and output
    def _ShowAverageFrame(n, f):
        text = ""
        if sNumPlanes == 1:
            average = f.props.PlaneAverage * valueRange + offset[0]
            text += "PlaneAverage[{plane}]={average}".format(plane=0, average=average)
        else:
            for p in range(sNumPlanes):
                average = f[p].props.PlaneAverage * valueRange + offset[p]
                text += "PlaneAverage[{plane}]={average}\n".format(plane=p, average=average)
        return core.text.Text(clip, text, alignment)
    
    avg = []
    for p in range(sNumPlanes):
        avg.append(PlaneAverage(clip, p))
    
    return core.std.FrameEval(clip, _ShowAverageFrame, prop_src=avg)
################################################################################################################################


################################################################################################################################
## Runtime function: FilterIf()
################################################################################################################################
## Take the frames from clip "flt" that is marked to be filtered and the ones from clip "src" that is not.
## An arbitrary frame property named "prop_name" from clip "props" is evaluated to determine whether it should be filtered.
################################################################################################################################
## Basic parameters
##     src {clip}: the source clip
##         can be of any constant format
##     flt {clip}: the filtered clip
##         must be of the same format and dimension as "src"
##     prop_name {str} (mandatory): the frame property to be evaluated
##         for each frame, if this property exists and is True, then take the frame from "flt", otherwise take the one from "src"
##     props {clip} (optional): the clip from which the frame property is evaluated
##         can be of any format, should have the same number of frames as "src"
##         default: None (use "src")
################################################################################################################################
def FilterIf(src, flt, prop_name, props=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'FilterIf'
    
    if not isinstance(src, vs.VideoNode):
        raise TypeError(funcName + ': \"src\" must be a clip!')
    if not isinstance(flt, vs.VideoNode):
        raise TypeError(funcName + ': \"flt\" must be a clip!')
    if props is not None and not isinstance(props, vs.VideoNode):
        raise TypeError(funcName + ': \"props\" must be a clip!')
    
    # Get properties of input clip
    sFormat = src.format
    if sFormat.id != flt.format.id:
        raise ValueError(funcName + ': \"src\" and \"flt\" must be of the same format!')
    if src.width != flt.width or src.height != flt.height:
        raise ValueError(funcName + ': \"src\" and \"flt\" must be of the same width and height!')
    
    if prop_name is None or not isinstance(prop_name, str):
        raise TypeError(funcName + ': \"prop_name\" must be specified and must be a str!')
    else:
        prop_name = _check_arg_prop(prop_name, None, None, 'prop_name', funcName)
    
    if props is None:
        props = src
    
    # FrameEval function
    def _FilterIfFrame(n, f):
        try:
            if f.props.__getattr__(prop_name):
                return flt
        except KeyError:
            pass
        return src
    
    # Process
    return core.std.FrameEval(src, _FilterIfFrame, props)
################################################################################################################################


################################################################################################################################
## Runtime function: FilterCombed()
################################################################################################################################
## Take the frames from clip "flt" that is marked as combed and the ones from clip "src" that is not.
## The frame property '_Combed' from clip "props" is evaluated to determine whether it's combed.
## This function is an instantiation of FilterIf()
################################################################################################################################
## Basic parameters
##     src {clip}: the source clip
##         can be of any constant format
##     flt {clip}: the filtered clip (de-interlaced)
##         must be of the same format and dimension as "src"
##     props {clip} (optional): the clip from which the frame property is evaluated
##         can be of any format, should have the same number of frames as "src"
##         default: None (use "src")
################################################################################################################################
def FilterCombed(src, flt, props=None):
    return FilterIf(src, flt, '_Combed', props)
################################################################################################################################





################################################################################################################################
################################################################################################################################
################################################################################################################################
## Utility functions below
################################################################################################################################
################################################################################################################################
################################################################################################################################


################################################################################################################################
## Utility function: Min()
################################################################################################################################
## Take 2 clips and return pixel-wise minimum of them.
## With a special mode=2 for difference clip.
################################################################################################################################
## Basic parameters
##     clip1 {clip}: the first clip
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 16/32 bit float
##     clip2 {clip}: the second clip
##         must be of the same format and dimension as "clip1"
##     mode {int[]}: specify processing mode for each plane
##         - 0: don't process, copy "clip1" to output
##         - 1: normal minimum, output = min(clip1, clip2)
##         - 2: difference minimum, output = abs(clip2 - neutral) < abs(clip1 - neutral) ? clip2 : clip1
##         default: [1,1,1] for YUV/RGB/YCoCg input, [1] for Gray input
################################################################################################################################
## Advanced parameters
##     neutral {int|float}: specfy the neutral value used for mode=2
##         default: 1 << (bits_per_sample - 1) for integer input, 0 for float input
################################################################################################################################
def Min(clip1, clip2, mode=None, neutral=None):
    return _operator2(clip1, clip2, mode, neutral, 'Min')
################################################################################################################################


################################################################################################################################
## Utility function: Max()
################################################################################################################################
## Take 2 clips and return pixel-wise maximum of them.
## With a special mode=2 for difference clip.
################################################################################################################################
## Basic parameters
##     clip1 {clip}: the first clip
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 16/32 bit float
##     clip2 {clip}: the second clip
##         must be of the same format and dimension as "clip1"
##     mode {int[]}: specify processing mode for each plane
##         - 0: don't process, copy "clip1" to output
##         - 1: normal maximum, output = max(clip1, clip2)
##         - 2: difference maximum, output = abs(clip2 - neutral) > abs(clip1 - neutral) ? clip2 : clip1
##         default: [1,1,1] for YUV/RGB/YCoCg input, [1] for Gray input
################################################################################################################################
## Advanced parameters
##     neutral {int|float}: specfy the neutral value used for mode=2
##         default: 1 << (bits_per_sample - 1) for integer input, 0 for float input
################################################################################################################################
def Max(clip1, clip2, mode=None, neutral=None):
    return _operator2(clip1, clip2, mode, neutral, 'Max')
################################################################################################################################


################################################################################################################################
## Utility function: Avg()
################################################################################################################################
## Take 2 clips and return pixel-wise average of them.
################################################################################################################################
## Basic parameters
##     clip1 {clip}: the first clip
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 16/32 bit float
##     clip2 {clip}: the second clip
##         must be of the same format and dimension as "clip1"
##     mode {int[]}: specify processing mode for each plane
##         - 0: don't process, copy "clip1" to output
##         - 1: average, output = (clip1 + clip2) / 2
##         default: [1,1,1] for YUV/RGB/YCoCg input, [1] for Gray input
################################################################################################################################
def Avg(clip1, clip2, mode=None):
    return _operator2(clip1, clip2, mode, None, 'Avg')
################################################################################################################################


################################################################################################################################
## Utility function: MinFilter()
################################################################################################################################
## Apply filtering with minimum difference of the 2 filtered clips.
################################################################################################################################
## Basic parameters
##     src {clip}: source clip
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 16/32 bit float
##     flt1 {clip}: filtered clip 1
##         must be of the same format and dimension as "src"
##     flt2 {clip}: filtered clip 2
##         must be of the same format and dimension as "src"
##     planes {int[]}: specify which planes to process
##         unprocessed planes will be copied from "src"
##         default: all planes will be processed, [0,1,2] for YUV/RGB/YCoCg input, [0] for Gray input
################################################################################################################################
def MinFilter(src, flt1, flt2, planes=None):
    return _min_max_filter(src, flt1, flt2, planes, 'MinFilter')
################################################################################################################################


################################################################################################################################
## Utility function: MaxFilter()
################################################################################################################################
## Apply filtering with maximum difference of the 2 filtered clips.
################################################################################################################################
## Basic parameters
##     src {clip}: source clip
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 16/32 bit float
##     flt1 {clip}: filtered clip 1
##         must be of the same format and dimension as "src"
##     flt2 {clip}: filtered clip 2
##         must be of the same format and dimension as "src"
##     planes {int[]}: specify which planes to process
##         unprocessed planes will be copied from "src"
##         default: all planes will be processed, [0,1,2] for YUV/RGB/YCoCg input, [0] for Gray input
################################################################################################################################
def MaxFilter(src, flt1, flt2, planes=None):
    return _min_max_filter(src, flt1, flt2, planes, 'MaxFilter')
################################################################################################################################


################################################################################################################################
## Utility function: LimitFilter()
################################################################################################################################
## Similar to the AviSynth function Dither_limit_dif16() and HQDeringmod_limit_dif16().
## It acts as a post-processor, and is very useful to limit the difference of filtering while avoiding artifacts.
## Commonly used cases:
##     de-banding
##     de-ringing
##     de-noising
##     sharpening
##     combining high precision source with low precision filtering: mvf.LimitFilter(src, flt, thr=1.0, elast=2.0)
################################################################################################################################
## There are 2 implementations, default one with std.Expr, the other with std.Lut.
## The Expr version supports all mode, while the Lut version doesn't support float input and ref clip.
## Also the Lut version will truncate the filtering diff if it exceeds half the value range(128 for 8-bit, 32768 for 16-bit).
## The Lut version might be faster than Expr version in some cases, for example 8-bit input and brighten_thr != thr.
################################################################################################################################
## Algorithm for Y/R/G/B plane (for chroma, replace "thr" and "brighten_thr" with "thrc")
##     dif = flt - src
##     dif_ref = flt - ref
##     dif_abs = abs(dif_ref)
##     thr_1 = brighten_thr if (dif > 0) else thr
##     thr_2 = thr_1 * elast
##
##     if dif_abs <= thr_1:
##         final = flt
##     elif dif_abs >= thr_2:
##         final = src
##     else:
##         final = src + dif * (thr_2 - dif_abs) / (thr_2 - thr_1)
################################################################################################################################
## Basic parameters
##     flt {clip}: filtered clip, to compute the filtering diff
##         can be of YUV/RGB/Gray/YCoCg color family, can be of 8-16 bit integer or 16/32 bit float
##     src {clip}: source clip, to apply the filtering diff
##         must be of the same format and dimension as "flt"
##     ref {clip} (optional): reference clip, to compute the weight to be applied on filtering diff
##         must be of the same format and dimension as "flt"
##         default: None (use "src")
##     thr {float}: threshold (8-bit scale) to limit filtering diff
##         default: 1.0
##     elast {float}: elasticity of the soft threshold
##         default: 2.0
##     planes {int[]}: specify which planes to process
##         unprocessed planes will be copied from "flt"
##         default: all planes will be processed, [0,1,2] for YUV/RGB/YCoCg input, [0] for Gray input
################################################################################################################################
## Advanced parameters
##     brighten_thr {float}: threshold (8-bit scale) for filtering diff that brightening the image (Y/R/G/B plane)
##         set a value different from "thr" is useful to limit the overshoot/undershoot/blurring introduced in sharpening/de-ringing
##         default is the same as "thr"
##     thrc {float}: threshold (8-bit scale) for chroma (U/V/Co/Cg plane)
##         default is the same as "thr"
##     force_expr {bool}
##         - True: force to use the std.Expr implementation
##         - False: use the std.Lut implementation if possible
##         default: True
################################################################################################################################
def LimitFilter(flt, src, ref=None, thr=None, elast=None, brighten_thr=None, thrc=None, force_expr=None, planes=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'LimitFilter'
    
    if not isinstance(flt, vs.VideoNode):
        raise TypeError(funcName + ': \"flt\" must be a clip!')
    if not isinstance(src, vs.VideoNode):
        raise TypeError(funcName + ': \"src\" must be a clip!')
    if ref is not None and not isinstance(ref, vs.VideoNode):
        raise TypeError(funcName + ': \"ref\" must be a clip!')
    
    # Get properties of input clip
    sFormat = flt.format
    if sFormat.id != src.format.id:
        raise ValueError(funcName + ': \"flt\" and \"src\" must be of the same format!')
    if flt.width != src.width or flt.height != src.height:
        raise ValueError(funcName + ': \"flt\" and \"src\" must be of the same width and height!')
    
    if ref is not None:
        if sFormat.id != ref.format.id:
            raise ValueError(funcName + ': \"flt\" and \"ref\" must be of the same format!')
        if flt.width != ref.width or flt.height != ref.height:
            raise ValueError(funcName + ': \"flt\" and \"ref\" must be of the same width and height!')
    
    sColorFamily = sFormat.color_family
    sIsYUV = sColorFamily == vs.YUV
    sIsYCOCG = sColorFamily == vs.YCOCG
    
    sSType = sFormat.sample_type
    sbitPS = sFormat.bits_per_sample
    sNumPlanes = sFormat.num_planes
    
    # Parameters
    if thr is None:
        thr = 1.0
    elif isinstance(thr, int) or isinstance(thr, float):
        if thr < 0:
            raise ValueError(funcName + ':valid range of \"thr\" is [0, +inf)')
    else:
        raise TypeError(funcName + ': \"thr\" must be an int or a float!')
    
    if elast is None:
        elast = 2.0
    elif isinstance(elast, int) or isinstance(elast, float):
        if elast < 1:
            raise ValueError(funcName + ':valid range of \"elast\" is [1, +inf)')
    else:
        raise TypeError(funcName + ': \"elast\" must be an int or a float!')
    
    if brighten_thr is None:
        brighten_thr = thr
    elif isinstance(brighten_thr, int) or isinstance(brighten_thr, float):
        if brighten_thr < 0:
            raise ValueError(funcName + ':valid range of \"brighten_thr\" is [0, +inf)')
    else:
        raise TypeError(funcName + ': \"brighten_thr\" must be an int or a float!')
    
    if thrc is None:
        thrc = thr
    elif isinstance(thrc, int) or isinstance(thrc, float):
        if thrc < 0:
            raise ValueError(funcName + ':valid range of \"thrc\" is [0, +inf)')
    else:
        raise TypeError(funcName + ': \"thrc\" must be an int or a float!')
    
    if force_expr is None:
        force_expr = True
    elif not isinstance(force_expr, int):
        raise TypeError(funcName + ': \"force_expr\" must be a bool!')
    if ref is not None or sSType != vs.INTEGER:
        force_expr = True
    
    # planes
    process = [0 for i in range(VSMaxPlaneNum)]
    
    if planes is None:
        process = [1 for i in range(VSMaxPlaneNum)]
    elif isinstance(planes, int):
        if planes < 0 or planes >= VSMaxPlaneNum:
            raise ValueError(funcName + ': valid range of \"planes\" is [0, {VSMaxPlaneNum})!'.format(VSMaxPlaneNum=VSMaxPlaneNum))
        process[planes] = 1
    elif isinstance(planes, list):
        for p in planes:
            if not isinstance(p, int):
                raise TypeError(funcName + ': \"planes\" must be a (list of) int!')
            elif p < 0 or p >= VSMaxPlaneNum:
                raise ValueError(funcName + ': valid range of \"planes\" is [0, {VSMaxPlaneNum})!'.format(VSMaxPlaneNum=VSMaxPlaneNum))
            process[p] = 1
    else:
        raise TypeError(funcName + ': \"planes\" must be a (list of) int!')
    
    # Process
    if thr <= 0 and brighten_thr <= 0:
        if sIsYUV or sIsYCOCG:
            if thrc <= 0:
                return src
        else:
            return src
    if thr >= 255 and brighten_thr >= 255:
        if sIsYUV or sIsYCOCG:
            if thrc >= 255:
                return flt
        else:
            return flt
    if thr >= 128 or brighten_thr >= 128:
        force_expr = True
    
    if force_expr: # implementation with std.Expr
        valueRange = (1 << sbitPS) - 1 if sSType == vs.INTEGER else 1
        limitExprY = _limit_filter_expr(ref is not None, thr, elast, brighten_thr, valueRange)
        limitExprC = _limit_filter_expr(ref is not None, thrc, elast, thrc, valueRange)
        expr = []
        for i in range(sNumPlanes):
            if process[i]:
                if i > 0 and (sIsYUV or sIsYCOCG):
                    expr.append(limitExprC)
                else:
                    expr.append(limitExprY)
            else:
                expr.append("")
        
        if ref is None:
            clip = core.std.Expr([flt, src], expr)
        else:
            clip = core.std.Expr([flt, src, ref], expr)
    else: # implementation with std.MakeDiff, std.Lut and std.MergeDiff
        diff = core.std.MakeDiff(flt, src, planes=planes)
        if sIsYUV or sIsYCOCG:
            if process[0]:
                diff = _limit_diff_lut(diff, thr, elast, brighten_thr, [0])
            if process[1] or process[2]:
                _planes = []
                if process[1]:
                    _planes.append(1)
                if process[2]:
                    _planes.append(2)
                diff = _limit_diff_lut(diff, thrc, elast, thrc, [1, 2])
        else:
            diff = _limit_diff_lut(diff, thr, elast, brighten_thr, planes)
        clip = core.std.MakeDiff(flt, diff, planes=planes)
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Utility function: PointPower()
################################################################################################################################
## Up-scaling by a power of 2 with nearest-neighborhood interpolation (point resize)
## Internally it involves std.Transpose, std.Interleave and std.DoubleWeave+std.SelectEvery for the scaling
## It will be faster than point-resizers for pure vertical scaling when the scaling ratio is not too large, especially for 8-bit input
################################################################################################################################
## Basic parameters
##     clip {clip}: clip to be scaled
##         can be of any format
##     vpow {float}: vertical scaling ratio is 2^vpow
##         default: 1
##     hpow {float}: horizontal scaling ratio is 2^hpow
##         default: 0
################################################################################################################################
def PointPower(clip, vpow=None, hpow=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'PointPower'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Parameters
    if vpow is None:
        vpow = 1
    elif not isinstance(vpow, int):
        raise TypeError(funcName + ': \"vpow\" must be an int!')
    elif vpow < 0:
        raise ValueError(funcName + ': valid range of \"vpow\" is [0, +inf)!')
    
    if hpow is None:
        hpow = 0
    elif not isinstance(hpow, int):
        raise TypeError(funcName + ': \"hpow\" must be an int!')
    elif hpow < 0:
        raise ValueError(funcName + ': valid range of \"hpow\" is [0, +inf)!')
    
    # Process
    if hpow > 0:
        clip = core.std.Transpose(clip)
        for i in range(hpow):
            clip = core.std.Interleave([clip, clip]).std.DoubleWeave(True).std.SelectEvery(2, 0)
        clip = core.std.Transpose(clip)
    
    if vpow > 0:
        for i in range(vpow):
            clip = core.std.Interleave([clip, clip]).std.DoubleWeave(True).std.SelectEvery(2, 0)
    
    # Output
    return AssumeFrame(clip)
################################################################################################################################


################################################################################################################################
## Utility function: CheckMatrix()
################################################################################################################################
## *** EXPERIMENTAL *** I'm not sure whether it will work or not ***
################################################################################################################################
## Check whether the input YUV clip matches specific color matrix
## Output is RGB24, out of range pixels will be marked as 255, indicating that the matrix may not be correct
## Additional plane mean values about the out of range pixels will be print on the frame
## Multiple matrices can be specified simultaneously, and multiple results will be stacked vertically
################################################################################################################################
## Basic parameters
##     clip {clip}: clip to evaluate
##         must be of YUV or YCoCg color family
##     matrices {str[]}: specify a (list of) matrix to test
##         default: ['601', '709', '2020', '240', 'FCC', 'YCgCo']
##     full {bool}: define if input clip is of full range
##         default: False for YUV input, True for YCoCg input
##     lower {float}: lower boundary for valid range (inclusive)
##         default: -0.02
##     upper {float}: upper boundary for valid range (inclusive)
##         default: 1.02
################################################################################################################################
def CheckMatrix(clip, matrices=None, full=None, lower=None, upper=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'CheckMatrix'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Get properties of input clip
    sFormat = clip.format
    
    sColorFamily = sFormat.color_family
    sIsRGB = sColorFamily == vs.RGB
    sIsYUV = sColorFamily == vs.YUV
    sIsGRAY = sColorFamily == vs.GRAY
    sIsYCOCG = sColorFamily == vs.YCOCG
    if sColorFamily == vs.COMPAT:
        raise ValueError(funcName + ': color family *COMPAT* is not supported!')
    if not (sIsYUV or sIsYCOCG):
        raise ValueError(funcName + ': only YUV or YCoCg color family is allowed!')
    
    # Parameters
    if matrices is None:
        matrices = ['601', '709', '2020', '240', 'FCC', 'YCgCo']
    elif not isinstance(matrices, list) and isinstance(matrices, str):
        raise TypeError(funcName + ': \'matrices\' must be a (list of) str!')
    
    if full is None:
        full = sIsYCOCG
    elif not isinstance(full, int):
        raise TypeError(funcName + ': \'full\' must be a bool!')
    
    if lower is None:
        lower = -0.02
    elif not (isinstance(lower, float) or isinstance(lower, int)):
        raise TypeError(funcName + ': \'lower\' must be an int or a float!')
    
    if upper is None:
        upper = 1.02
    elif not (isinstance(upper, float) or isinstance(upper, int)):
        raise TypeError(funcName + ': \'upper\' must be an int or a float!')
    
    # Process
    clip = ToYUV(clip, css='444', depth=32, full=full)
    
    props = ['RMean', 'GMean', 'BMean', 'TotalMean']
    def _FrameProps(n, f):
        fout = f.copy()
        fout.props.__setattr__(props[3], (f.props.__getattr__(props[0]) + f.props.__getattr__(props[1]) + f.props.__getattr__(props[2])) / 3)
        return fout
    
    rgb_clips = []
    for matrix in matrices:
        rgb_clip = ToRGB(clip, matrix=matrix)
        rgb_clip = rgb_clip.std.Expr('x {lower} < 1 x - x {upper} > x 0 ? ?'.format(lower=lower, upper=upper))
        rgb_clip = PlaneAverage(rgb_clip, 0, props[0])
        rgb_clip = PlaneAverage(rgb_clip, 1, props[1])
        rgb_clip = PlaneAverage(rgb_clip, 2, props[2])
        rgb_clip = core.std.ModifyFrame(rgb_clip, rgb_clip, selector=_FrameProps)
        rgb_clip = Depth(rgb_clip, depth=8, dither='none')
        rgb_clip = rgb_clip.text.FrameProps(props, alignment=7)
        rgb_clip = rgb_clip.text.Text(matrix, alignment=8)
        rgb_clips.append(rgb_clip)
    
    # Output
    return core.std.StackVertical(rgb_clips)
################################################################################################################################


################################################################################################################################
## Utility function: postfix2infix()
################################################################################################################################
## Convert postfix expression (used by std.Expr) to infix expression
################################################################################################################################
## Basic parameters
##     expr {str}: the postfix expression to be converted
################################################################################################################################
def postfix2infix(expr):
    funcName = 'postfix2infix'
    op1 = ['exp', 'log', 'sqrt', 'abs', 'not', 'dup']
    op2 = ['+', '-', '*', '/', 'max', 'min', '>', '<', '=', '>=', '<=', 'and', 'or', 'xor', 'swap', 'pow']
    op3 = ['?']

    def remove_brackets(x):
        if x[0] == '(' and x[len(x) - 1] == ')':
            p = 1
            for c in x[1:-1]:
                if c == '(':
                    p += 1
                elif c == ')':
                    p -= 1
                if p == 0:
                    break
            if p == 1:
                return x[1:-1]
        return x

    if not isinstance(expr, str):
        raise TypeError(funcName + ': \'expr\' must be a str!')
    expr_list = expr.split()

    stack = []
    for item in expr_list:
        if op1.count(item) > 0:
            try:
                operand1 = stack.pop()
            except IndexError:
                raise ValueError(funcName + ': Invalid expression, require operands.')
            if item == 'dup':
                stack.append(operand1)
                stack.append(operand1)
            else:
                stack.append('{}({})'.format(item, remove_brackets(operand1)))
        elif op2.count(item) > 0:
            try:
                operand2 = stack.pop()
                operand1 = stack.pop()
            except IndexError:
                raise ValueError(funcName + ': Invalid expression, require operands.')
            stack.append('({} {} {})'.format(operand1, item, operand2))
        elif op3.count(item) > 0:
            try:
                operand3 = stack.pop()
                operand2 = stack.pop()
                operand1 = stack.pop()
            except IndexError:
                raise ValueError(funcName + ': Invalid expression, require operands.')
            stack.append('({} {} {} {} {})'.format(operand1, item, operand2, ':', operand3))
        else:
            stack.append(item)

    if len(stack) > 1:
        raise ValueError(funcName + ': Invalid expression, require operators.')
    return remove_brackets(stack[0])
################################################################################################################################





################################################################################################################################
################################################################################################################################
################################################################################################################################
## Frame property functions below
################################################################################################################################
################################################################################################################################
################################################################################################################################


################################################################################################################################
## Frame property function: SetColorSpace()
################################################################################################################################
## Modify the color space related frame properties in the given clip.
## Detailed descriptions of these properties: http://www.vapoursynth.com/doc/apireference.html
################################################################################################################################
## Parameters
##     %Any%: for the property named "_%Any%"
##         - None: do nothing
##         - True: do nothing
##         - False: delete corresponding frame properties if exist
##         - {int}: set to this value
################################################################################################################################
def SetColorSpace(clip, ChromaLocation=None, ColorRange=None, Primaries=None, Matrix=None, Transfer=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'SetColorSpace'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Modify frame properties
    if ChromaLocation is None:
        pass
    elif isinstance(ChromaLocation, bool):
        if ChromaLocation is False:
            clip = core.std.SetFrameProp(clip, prop='_ChromaLocation', delete=True)
    elif isinstance(ChromaLocation, int):
        if ChromaLocation >= 0 and ChromaLocation <=5:
            clip = core.std.SetFrameProp(clip, prop='_ChromaLocation', intval=ChromaLocation)
        else:
            raise ValueError(funcName + ': valid range of \"ChromaLocation\" is [0, 5]!')
    else:
        raise TypeError(funcName + ': \"ChromaLocation\" must be an int or a bool!')
    
    if ColorRange is None:
        pass
    elif isinstance(ColorRange, bool):
        if ColorRange is False:
            clip = core.std.SetFrameProp(clip, prop='_ColorRange', delete=True)
    elif isinstance(ColorRange, int):
        if ColorRange >= 0 and ColorRange <=1:
            clip = core.std.SetFrameProp(clip, prop='_ColorRange', intval=ColorRange)
        else:
            raise ValueError(funcName + ': valid range of \"ColorRange\" is [0, 1]!')
    else:
        raise TypeError(funcName + ': \"ColorRange\" must be an int or a bool!')
    
    if Primaries is None:
        pass
    elif isinstance(Primaries, bool):
        if Primaries is False:
            clip = core.std.SetFrameProp(clip, prop='_Primaries', delete=True)
    elif isinstance(Primaries, int):
        clip = core.std.SetFrameProp(clip, prop='_Primaries', intval=Primaries)
    else:
        raise TypeError(funcName + ': \"Primaries\" must be an int or a bool!')
    
    if Matrix is None:
        pass
    elif isinstance(Matrix, bool):
        if Matrix is False:
            clip = core.std.SetFrameProp(clip, prop='_Matrix', delete=True)
    elif isinstance(Matrix, int):
        clip = core.std.SetFrameProp(clip, prop='_Matrix', intval=Matrix)
    else:
        raise TypeError(funcName + ': \"Matrix\" must be an int or a bool!')
    
    if Transfer is None:
        pass
    elif isinstance(Transfer, bool):
        if Transfer is False:
            clip = core.std.SetFrameProp(clip, prop='_Transfer', delete=True)
    elif isinstance(Transfer, int):
        clip = core.std.SetFrameProp(clip, prop='_Transfer', intval=Transfer)
    else:
        raise TypeError(funcName + ': \"Transfer\" must be an int or a bool!')
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Frame property function: AssumeFrame()
################################################################################################################################
## Set all the frames in the given clip to be frame-based(progressive).
## It can be used to prevent the field order set in de-interlace filters from being overridden by the frame property '_FieldBased'.
## Also it may be useful to be applied before upscaling or anti-aliasing scripts using EEDI3/nnedi3, etc.(whose field order should be specified explicitly)
################################################################################################################################
def AssumeFrame(clip):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'AssumeFrame'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Modify frame properties
    clip = core.std.SetFrameProp(clip, prop='_FieldBased', intval=0)
    clip = core.std.SetFrameProp(clip, prop='_Field', delete=True)
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Frame property function: AssumeTFF()
################################################################################################################################
## Set all the frames in the given clip to be top-field-first(interlaced).
## This frame property will override the field order set in those de-interlace filters.
################################################################################################################################
def AssumeTFF(clip):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'AssumeTFF'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Modify frame properties
    clip = core.std.SetFrameProp(clip, prop='_FieldBased', intval=2)
    clip = core.std.SetFrameProp(clip, prop='_Field', delete=True)
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Frame property function: AssumeBFF()
################################################################################################################################
## Set all the frames in the given clip to be bottom-field-first(interlaced).
## This frame property will override the field order set in those de-interlace filters.
################################################################################################################################
def AssumeBFF(clip):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'AssumeBFF'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Modify frame properties
    clip = core.std.SetFrameProp(clip, prop='_FieldBased', intval=1)
    clip = core.std.SetFrameProp(clip, prop='_Field', delete=True)
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Frame property function: AssumeField()
################################################################################################################################
## Set all the frames in the given clip to be field-based(derived from interlaced frame).
################################################################################################################################
## Parameters
##     top {bool}:
##         - True: top-field-based
##         - False: bottom-field-based
################################################################################################################################
def AssumeField(clip, top):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'AssumeField'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    if not isinstance(top, int):
        raise TypeError(funcName + ': \"top\" must be a bool!')
    
    # Modify frame properties
    clip = core.std.SetFrameProp(clip, prop='_FieldBased', delete=True)
    clip = core.std.SetFrameProp(clip, prop='_Field', intval=1 if top else 0)
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Frame property function: AssumeCombed()
################################################################################################################################
## Set all the frames in the given clip to be combed or not.
################################################################################################################################
## Parameters
##     combed {bool}:
##         - None: delete property '_Combed' if exist
##         - True: set property '_Combed' to 1
##         - False: set property '_Combed' to 0
##         default: True
################################################################################################################################
def AssumeCombed(clip, combed=True):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'AssumeCombed'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Modify frame properties
    if combed is None:
        clip = core.std.SetFrameProp(clip, prop='_Combed', delete=True)
    elif not isinstance(combed, int):
        raise TypeError(funcName + ': \"combed\" must be a bool!')
    else:
        clip = core.std.SetFrameProp(clip, prop='_Combed', intval=combed)
    
    # Output
    return clip
################################################################################################################################





################################################################################################################################
################################################################################################################################
################################################################################################################################
## Helper functions below
################################################################################################################################
################################################################################################################################
################################################################################################################################


################################################################################################################################
## Helper function: CheckVersion()
################################################################################################################################
## Check if the version of mvsfunc matches the specified version requirements.
## For example, if you write a script requires at least mvsfunc-r5, use mvf.CheckVersion(5) to make sure r5 or later version is used.
################################################################################################################################
## Parameters
##     version {int} (mandatory): specify the required version number
##     less {bool}: if False, raise error when this mvsfunc's version is less than the specified version
##         default: False
##     equal {bool}: if False, raise error when this mvsfunc's version is equal to the specified version
##         default: True
##     greater {bool}: if False, raise error when this mvsfunc's version is greater than the specified version
##         default: True
################################################################################################################################
def CheckVersion(version, less=False, equal=True, greater=True):
    funcName = 'CheckVersion'
    
    if not less and MvsFuncVersion < version:
        raise ImportWarning('mvsfunc version(={0}) is less than the version(={1}) specified!'.format(MvsFuncVersion, version))
    if not equal and MvsFuncVersion == version:
        raise ImportWarning('mvsfunc version(={0}) is equal to the version(={1}) specified!'.format(MvsFuncVersion, version))
    if not greater and MvsFuncVersion > version:
        raise ImportWarning('mvsfunc version(={0}) is greater than the version(={1}) specified!'.format(MvsFuncVersion, version))
    
    return True
################################################################################################################################


################################################################################################################################
## Helper function: GetMatrix()
################################################################################################################################
## Return str or int format parameter "matrix".
################################################################################################################################
## Parameters
##     clip {clip}: the source clip
##     matrix {int|str}: explicitly specify matrix in int or str format, not case-sensitive
##         - 0 | "RGB"
##         - 1 | "709" | "bt709"
##         - 2 | "Unspecified": same as not specified (None)
##         - 4 | "FCC"
##         - 5 | "bt470bg": same as "601"
##         - 6 | "601" | "smpte170m"
##         - 7 | "240" | "smpte240m"
##         - 8 | "YCgCo" | "YCoCg"
##         - 9 | "2020" | "bt2020nc"
##         - 10 | "2020cl" | "bt2020c"
##         - 100 | "OPP" | "opponent": same as the opponent color space used in BM3D denoising filter
##         default: guessed according to the color family and size of input clip
##     dIsRGB {bool}: specify if the target is RGB
##         If source and target are both RGB and "matrix" is not specified, then assume matrix="RGB".
##         default: False for RGB input, otherwise True
##     id {bool}:
##         - False: output matrix name{str}
##         - True: output matrix id{int}
##         default: False
################################################################################################################################
def GetMatrix(clip, matrix=None, dIsRGB=None, id=False):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'GetMatrix'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Get properties of input clip
    sFormat = clip.format
    
    sColorFamily = sFormat.color_family
    sIsRGB = sColorFamily == vs.RGB
    sIsYUV = sColorFamily == vs.YUV
    sIsGRAY = sColorFamily == vs.GRAY
    sIsYCOCG = sColorFamily == vs.YCOCG
    if sColorFamily == vs.COMPAT:
        raise ValueError(funcName + ': color family *COMPAT* is not supported!')
    
    # Get properties of output clip
    if dIsRGB is None:
        dIsRGB = not sIsRGB
    elif not isinstance(dIsRGB, int):
        raise TypeError(funcName + ': \"dIsRGB\" must be a bool!')
    
    # id
    if not isinstance(id, int):
        raise TypeError(funcName + ': \"id\" must be a bool!')
    
    # Resolution level
    noneD = False
    SD = False
    HD = False
    UHD = False
    
    if clip.width <= 0 or clip.height <= 0:
        noneD = True
    elif clip.width <= 1024 and clip.height <= 576:
        SD = True
    elif clip.width <= 2048 and clip.height <= 1536:
        HD = True
    else:
        UHD = True
    
    # Convert to string format
    if matrix is None:
        matrix = "Unspecified"
    elif not isinstance(matrix, int) and not isinstance(matrix, str):
        raise TypeError(funcName + ': \"matrix\" must be an int or a str!')
    else:
        if isinstance(matrix, str):
            matrix = matrix.lower()
        if matrix == 0 or matrix == "rgb": # GBR
            matrix = 0 if id else "RGB"
        elif matrix == 1 or matrix == "709" or matrix == "bt709": # bt709
            matrix = 1 if id else "709"
        elif matrix == 2 or matrix == "unspecified" or matrix == "unspec": # Unspecified
            matrix = 2 if id else "Unspecified"
        elif matrix == 4 or matrix == "fcc": # fcc
            matrix = 4 if id else "FCC"
        elif matrix == 5 or matrix == "bt470bg" or matrix == "470bg": # bt470bg
            matrix = 5 if id else "601"
        elif matrix == 6 or matrix == "601" or matrix == "smpte170m" or matrix == "170m": # smpte170m
            matrix = 6 if id else "601"
        elif matrix == 7 or matrix == "240" or matrix == "smpte240m": # smpte240m
            matrix = 7 if id else "240"
        elif matrix == 8 or matrix == "ycgco" or matrix == "ycocg": # YCgCo
            matrix = 8 if id else "YCgCo"
        elif matrix == 9 or matrix == "2020" or matrix == "bt2020nc" or matrix == "2020ncl": # bt2020nc
            matrix = 9 if id else "2020"
        elif matrix == 10 or matrix == "2020cl" or matrix == "bt2020c": # bt2020c
            matrix = 10 if id else "2020cl"
        elif matrix == 100 or matrix == "opp" or matrix == "opponent": # opponent color space
            matrix = 100 if id else "OPP"
        else:
            raise ValueError(funcName + ': Unsupported matrix specified!')
    
    # If unspecified, automatically determine it based on color family and resolution level
    if matrix == 2 or matrix == "Unspecified":
        if dIsRGB and sIsRGB:
            matrix = 0 if id else "RGB"
        elif sIsYCOCG:
            matrix = 8 if id else "YCgCo"
        else:
            matrix = (6 if id else "601") if SD else (9 if id else "2020") if UHD else (1 if id else "709")
    
    # Output
    return matrix
################################################################################################################################


################################################################################################################################
## Helper function: zDepth()
################################################################################################################################
## Smart function to utilize zimg depth conversion for both 1.0 and 2.0 API of vszimg as well as core.resize.
## core.resize is preferred now.
################################################################################################################################
def zDepth(clip, sample=None, depth=None, range=None, range_in=None, dither_type=None, cpu_type=None, prefer_props=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'zDepth'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Get properties of input clip
    sFormat = clip.format
    
    # Get properties of output clip
    if sample is None:
        sample = sFormat.sample_type
    elif not isinstance(sample, int):
        raise TypeError(funcName + ': \"sample\" must be an int!')
    
    if depth is None:
        depth = sFormat.bits_per_sample
    elif not isinstance(depth, int):
        raise TypeError(funcName + ': \"depth\" must be an int!')
    
    format = core.register_format(sFormat.color_family, sample, depth, sFormat.subsampling_w, sFormat.subsampling_h)
    
    # Process
    zimgResize = core.version_number() >= 29
    zimgPlugin = core.get_plugins().__contains__('the.weather.channel')
    if zimgResize:
        clip = core.resize.Bicubic(clip, format=format.id, range=range, range_in=range_in, dither_type=dither_type, prefer_props=prefer_props)
    elif zimgPlugin and core.z.get_functions().__contains__('Format'):
        clip = core.z.Format(clip, format=format.id, range=range, range_in=range_in, dither_type=dither_type, cpu_type=cpu_type)
    elif zimgPlugin and core.z.get_functions().__contains__('Depth'):
        clip = core.z.Depth(clip, dither=dither_type, sample=sample, depth=depth, fullrange_in=range_in, fullrange_out=range)
    else:
        raise AttributeError(funcName + ': Available zimg not found!')
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Helper function: PlaneAverage()
################################################################################################################################
## Evaluate normalized average value of the specified plane and store it as a frame property.
## Mainly as a wrap function to support both std.PlaneAverage and std.PlaneStats with the same interface.
################################################################################################################################
## Parameters
##     clip {clip}: the source clip
##         can be of any constant format
##     plane {int}: the plane to evaluate
##         default: 0
##     prop {str}: the frame property name to be written
##         default: 'PlaneAverage'
################################################################################################################################
def PlaneAverage(clip, plane=None, prop=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'PlaneAverage'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Get properties of input clip
    sFormat = clip.format
    sNumPlanes = sFormat.num_planes
    
    # Parameters
    if plane is None:
        plane = 0
    elif not isinstance(plane, int):
        raise TypeError(funcName + ': \"plane\" must be an int!')
    elif plane < 0 or plane > sNumPlanes:
        raise ValueError(funcName + ': valid range of \"plane\" is [0, {})!'.format(sNumPlanes))
    
    if prop is None:
        prop = 'PlaneAverage'
    elif not isinstance(prop, str):
        raise TypeError(funcName + ': \"prop\" must be a str!')
    
    # Process
    if core.std.get_functions().__contains__('PlaneAverage'):
        clip = core.std.PlaneAverage(clip, plane=plane, prop=prop)
    elif core.std.get_functions().__contains__('PlaneStats'):
        clip = core.std.PlaneStats(clip, plane=plane, prop='PlaneStats')
        def _PlaneAverageTransfer(n, f):
            fout = f.copy()
            fout.props.__setattr__(prop, f.props.PlaneStatsAverage)
            del fout.props.PlaneStatsAverage
            del fout.props.PlaneStatsMinMax
            return fout
        clip = core.std.ModifyFrame(clip, clip, selector=_PlaneAverageTransfer)
    else:
        raise AttributeError(funcName + ': Available plane average function not found!')
    
    # output
    return clip
################################################################################################################################


################################################################################################################################
## Helper function: GetPlane()
################################################################################################################################
## Extract specific plane and store it in a Gray clip.
################################################################################################################################
## Parameters
##     clip {clip}: the source clip
##         can be of any constant format
##     plane {int}: the plane to extract
##         default: 0
################################################################################################################################
def GetPlane(clip, plane=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'GetPlane'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Get properties of input clip
    sFormat = clip.format
    sNumPlanes = sFormat.num_planes
    
    # Parameters
    if plane is None:
        plane = 0
    elif not isinstance(plane, int):
        raise TypeError(funcName + ': \"plane\" must be an int!')
    elif plane < 0 or plane > sNumPlanes:
        raise ValueError(funcName + ': valid range of \"plane\" is [0, {})!'.format(sNumPlanes))
    
    # Process
    return core.std.ShufflePlanes(clip, plane, vs.GRAY)
################################################################################################################################


################################################################################################################################
## Helper function: GrayScale()
################################################################################################################################
## Convert the given clip to gray-scale.
################################################################################################################################
## Parameters
##     clip {clip}: the source clip
##         can be of any constant format
##     matrix {int|str}: for RGB input only, same as the one in ToYUV()
################################################################################################################################
def GrayScale(clip, matrix=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'GrayScale'
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Get properties of input clip
    sFormat = clip.format
    
    sColorFamily = sFormat.color_family
    sIsRGB = sColorFamily == vs.RGB
    sIsYUV = sColorFamily == vs.YUV
    sIsGRAY = sColorFamily == vs.GRAY
    sIsYCOCG = sColorFamily == vs.YCOCG
    if sColorFamily == vs.COMPAT:
        raise ValueError(funcName + ': color family *COMPAT* is not supported!')
    
    # Process
    if sIsGRAY:
        pass
    elif sIsRGB:
        clip = ToYUV(clip, matrix=matrix, full=True)
        clip = core.std.ShufflePlanes(clip, [0,0,0], sColorFamily)
    else:
        blank = clip.std.BlankClip()
        clip = core.std.ShufflePlanes([clip,blank,blank], [0,1,2], sColorFamily)
    
    # Output
    return clip
################################################################################################################################


################################################################################################################################
## Helper function: Preview()
################################################################################################################################
## Convert the given clip or clips to the same RGB format.
## When multiple clips is given, they will be interleaved together, and resized to the same dimension using "Catmull-Rom".
################################################################################################################################
## Set "plane" if you want to output a specific plane.
## Set compat=True for COMPATBGR32 format output, usually used for VirtualDub, AvsPmod, etc.
################################################################################################################################
## matrix, full, dither, kernel, a1, a2, prefer_props correspond to matrix_in, range_in, dither_type,
## resample_filter_uv, filter_param_a_uv, filter_param_b_uv, prefer_props in resize.Bicubic.
## "matrix" is passed to GetMatrix(id=True) first.
## default dither: random
## default chroma resampler: kernel="bicubic", a1=0, a2=0.5, also known as "Catmull-Rom"
################################################################################################################################
def Preview(clips, plane=None, compat=None, matrix=None, full=None, depth=None,\
dither=None, kernel=None, a1=None, a2=None, prefer_props=None):
    # Set VS core and function name
    core = vs.get_core()
    funcName = 'Preview'
    
    if isinstance(clips, vs.VideoNode):
        ref = clips
    elif isinstance(clips, list):
        for c in clips:
            if not isinstance(c, vs.VideoNode):
                raise TypeError(funcName + ': \"clips\" must be a clip or a list of clips!')
        ref = clips[0]
    else:
        raise TypeError(funcName + ': \"clips\" must be a clip or a list of clips!')
    
    # Get properties of output clip
    if compat:
        dFormat = vs.COMPATBGR32
    else:
        if depth is None:
            depth = 8
        elif not isinstance(depth, int):
            raise TypeError(funcName + ': \"depth\" must be an int!')
        if depth >= 32:
            sample = vs.FLOAT
        else:
            sample = vs.INTEGER
        dFormat = core.register_format(vs.RGB, sample, depth, 0, 0).id
    
    # Parameters
    if dither is None:
        dither = "random"
    if kernel is None:
        kernel = "bicubic"
        if a1 is None and a2 is None:
            a1 = 0
            a2 = 0.5
    
    # Conversion
    def _Conv(clip):
        if plane is not None:
            clip = GetPlane(clip, plane)
        return core.resize.Bicubic(clip, ref.width, ref.height, format=dFormat, matrix_in=GetMatrix(clip, matrix, True, True), range_in=full, filter_param_a=0, filter_param_b=0.5, resample_filter_uv=kernel, filter_param_a_uv=a1, filter_param_b_uv=a2, dither_type=dither, prefer_props=prefer_props)
    
    if isinstance(clips, vs.VideoNode):
        clip = _Conv(clips)
    elif isinstance(clips, list):
        for i in range(len(clips)):
            clips[i] = _Conv(clips[i])
        clip = core.std.Interleave(clips)
    
    # Output
    return clip
################################################################################################################################





################################################################################################################################
################################################################################################################################
################################################################################################################################
## Internal used functions below
################################################################################################################################
################################################################################################################################
################################################################################################################################


################################################################################################################################
## Internal used function to calculate quantization parameters
################################################################################################################################
def _quantization_parameters(sample=None, depth=None, full=None, chroma=None, funcName='_quantization_parameters'):
    qp = {}
    
    if sample is None:
        sample = vs.INTEGER
    if depth is None:
        depth = 8
    elif depth < 1:
        raise ValueError(funcName + ': \"depth\" should not be less than 1!')
    if full is None:
        full = True
    if chroma is None:
        chroma = False
    
    lShift = depth - 8
    rShift = 8 - depth
    
    if sample == vs.INTEGER:
        if chroma:
            qp['floor'] = 0 if full else 16 << lShift if lShift >= 0 else 16 >> rShift
            qp['neutral'] = 128 << lShift if lShift >= 0 else 128 >> rShift
            qp['ceil'] = (1 << depth) - 1 if full else 240 << lShift if lShift >= 0 else 240 >> rShift
            qp['range'] = qp['ceil'] - qp['floor']
        else:
            qp['floor'] = 0 if full else 16 << lShift if lShift >= 0 else 16 >> rShift
            qp['neutral'] = qp['floor']
            qp['ceil'] = (1 << depth) - 1 if full else 235 << lShift if lShift >= 0 else 235 >> rShift
            qp['range'] = qp['ceil'] - qp['floor']
    elif sample == vs.FLOAT:
        if chroma:
            qp['floor'] = -0.5
            qp['neutral'] = 0.0
            qp['ceil'] = 0.5
            qp['range'] = qp['ceil'] - qp['floor']
        else:
            qp['floor'] = 0.0
            qp['neutral'] = qp['floor']
            qp['ceil'] = 1.0
            qp['range'] = qp['ceil'] - qp['floor']
    else:
        raise ValueError(funcName + ': Unsupported \"sample\" specified!')
    
    return qp
################################################################################################################################


################################################################################################################################
## Internal used function to do quantization conversion with std.Expr
################################################################################################################################
def _quantization_conversion(clip, depths=None, depthd=None, sample=None, fulls=None, fulld=None, chroma=None,\
clamp=None, dbitPS=None, mode=None, funcName='_quantization_conversion'):
    # Set VS core and function name
    core = vs.get_core()
    
    if not isinstance(clip, vs.VideoNode):
        raise TypeError(funcName + ': \"clip\" must be a clip!')
    
    # Get properties of input clip
    sFormat = clip.format
    
    sColorFamily = sFormat.color_family
    sIsRGB = sColorFamily == vs.RGB
    sIsYUV = sColorFamily == vs.YUV
    sIsGRAY = sColorFamily == vs.GRAY
    sIsYCOCG = sColorFamily == vs.YCOCG
    if sColorFamily == vs.COMPAT:
        raise ValueError(funcName + ': color family *COMPAT* is not supported!')
    
    sbitPS = sFormat.bits_per_sample
    sSType = sFormat.sample_type
    
    if depths is None:
        depths = sbitPS
    elif not isinstance(depths, int):
        raise TypeError(funcName + ': \"depths\" must be an int!')
    
    if fulls is None:
        # If not set, assume limited range for YUV and Gray input
        fulls = False if sIsYUV or sIsGRAY else True
    elif not isinstance(fulls, int):
        raise TypeError(funcName + ': \"fulls\" must be a bool!')
    
    if chroma is None:
        chroma = False
    elif not isinstance(chroma, int):
        raise TypeError(funcName + ': \"chroma\" must be a bool!')
    elif not sIsGRAY:
        chroma = False
    
    # Get properties of output clip
    if depthd is None:
        pass
    elif not isinstance(depthd, int):
        raise TypeError(funcName + ': \"depthd\" must be an int!')
    if sample is None:
        if depthd is None:
            dSType = sSType
            depthd = depths
        else:
            dSType = vs.FLOAT if dbitPS >= 32 else vs.INTEGER
    elif not isinstance(sample, int):
        raise TypeError(funcName + ': \"sample\" must be an int!')
    elif sample != vs.INTEGER and sample != vs.FLOAT:
        raise ValueError(funcName + ': \"sample\" must be either 0(vs.INTEGER) or 1(vs.FLOAT)!')
    else:
        dSType = sample
    if dSType == vs.INTEGER and (dbitPS < 1 or dbitPS > 16):
        raise ValueError(funcName + ': {0}-bit integer output is not supported!'.format(dbitPS))
    if dSType == vs.FLOAT and (dbitPS != 16 and dbitPS != 32):
        raise ValueError(funcName + ': {0}-bit float output is not supported!'.format(dbitPS))
    
    if fulld is None:
        fulld = fulls
    elif not isinstance(fulld, int):
        raise TypeError(funcName + ': \"fulld\" must be a bool!')
    
    if clamp is None:
        clamp = dSType == vs.INTEGER
    elif not isinstance(clamp, int):
        raise TypeError(funcName + ': \"clamp\" must be a bool!')
    
    if dbitPS is None:
        if depthd < 8:
            dbitPS = 8
        else:
            dbitPS = depthd
    elif not isinstance(dbitPS, int):
        raise TypeError(funcName + ': \"dbitPS\" must be an int!')
    
    if mode is None:
        mode = 0
    elif not isinstance(mode, int):
        raise TypeError(funcName + ': \"mode\" must be an int!')
    elif depthd >= 8:
        mode = 0
    
    dFormat = core.register_format(sFormat.color_family, dSType, dbitPS, sFormat.subsampling_w, sFormat.subsampling_h)
    
    # Expression function
    def gen_expr(chroma, mode):
        if dSType == vs.INTEGER:
            exprLower = 0
            exprUpper = 1 << (dFormat.bytes_per_sample * 8) - 1
        else:
            exprLower = float('-inf')
            exprUpper = float('inf')
        
        sQP = _quantization_parameters(sSType, depths, fulls, chroma, funcName)
        dQP = _quantization_parameters(dSType, depthd, fulld, chroma, funcName)
        
        gain = dQP['range'] / sQP['range']
        offset = dQP['neutral' if chroma else 'floor'] - sQP['neutral' if chroma else 'floor'] * gain
        
        if mode == 1:
            scale = 256
            gain = gain * scale
            offset = offset * scale
        else:
            scale = 1
        
        if gain != 1 or offset != 0 or clamp:
            expr = " x "
            if gain != 1: expr = expr + " {} * ".format(gain)
            if offset != 0: expr = expr + " {} + ".format(offset)
            if clamp:
                if dQP['floor'] * scale > exprLower: expr = expr + " {} max ".format(dQP['floor'] * scale)
                if dQP['ceil'] * scale < exprUpper: expr = expr + " {} min ".format(dQP['ceil'] * scale)
        else:
            expr = ""
        
        return expr
    
    # Process
    Yexpr = gen_expr(False, mode)
    Cexpr = gen_expr(True, mode)
    
    if sIsYUV or sIsYCOCG:
        expr = [Yexpr, Cexpr]
    elif sIsGRAY and chroma:
        expr = Cexpr
    else:
        expr = Yexpr
    
    clip = core.std.Expr(clip, expr, format=dFormat.id)
    
    # Output
    clip = SetColorSpace(clip, ColorRange=0 if fulld else 1)
    return clip
################################################################################################################################


################################################################################################################################
## Internal used function to check the argument for frame property
################################################################################################################################
def _check_arg_prop(arg, default=None, defaultTrue=None, argName='arg', funcName='_check_arg_prop'):
    if defaultTrue is None:
        defaultTrue = default
    
    if arg is None:
        arg = default
    elif isinstance(arg, int):
        if arg:
            arg = defaultTrue
    elif isinstance(arg, str):
        if arg:
            if not arg.isidentifier():
                raise ValueError(funcName + ': {argName}=\"{arg}\" is not a valid identifier!'.format(argName=argName, arg=arg))
        else:
            arg = False
    else:
        raise TypeError(funcName + ': \"{argName}\" must be a str or a bool!'.format(argName=argName))
    
    return arg
################################################################################################################################


################################################################################################################################
## Internal used function for Min(), Max() and Avg()
################################################################################################################################
def _operator2(clip1, clip2, mode, neutral, funcName):
    # Set VS core
    core = vs.get_core()
    
    if not isinstance(clip1, vs.VideoNode):
        raise TypeError(funcName + ': \"clip1\" must be a clip!')
    if not isinstance(clip2, vs.VideoNode):
        raise TypeError(funcName + ': \"clip2\" must be a clip!')
    
    # Get properties of input clip
    sFormat = clip1.format
    if sFormat.id != clip2.format.id:
        raise ValueError(funcName + ': \"clip1\" and \"clip2\" must be of the same format!')
    if clip1.width != clip2.width or clip1.height != clip2.height:
        raise ValueError(funcName + ': \"clip1\" and \"clip2\" must be of the same width and height!')
    
    sSType = sFormat.sample_type
    sbitPS = sFormat.bits_per_sample
    sNumPlanes = sFormat.num_planes
    
    # mode
    if mode is None:
        mode = [1 for i in range(VSMaxPlaneNum)]
    elif isinstance(mode, int):
        mode = [mode for i in range(VSMaxPlaneNum)]
    elif isinstance(mode, list):
        for m in mode:
            if not isinstance(m, int):
                raise TypeError(funcName + ': \"mode\" must be a (list of) int!')
        while len(mode) < VSMaxPlaneNum:
            mode.append(mode[len(mode) - 1])
    else:
        raise TypeError(funcName + ': \"mode\" must be a (list of) int!')
    
    # neutral
    if neutral is None:
        neutral = 1 << (sbitPS - 1) if sSType == vs.INTEGER else 0
    elif not (isinstance(neutral, int) or isinstance(neutral, float)):
        raise TypeError(funcName + ': \"neutral\" must be an int or a float!')
    
    # Process and output
    expr = []
    for i in range(sNumPlanes):
        if funcName == 'Min':
            if mode[i] >= 2:
                expr.append("y {neutral} - abs x {neutral} - abs < y x ?".format(neutral=neutral))
            elif mode[i] == 1:
                expr.append("x y min")
            else:
                expr.append("")
        elif funcName == 'Max':
            if mode[i] >= 2:
                expr.append("y {neutral} - abs x {neutral} - abs > y x ?".format(neutral=neutral))
            elif mode[i] == 1:
                expr.append("x y max")
            else:
                expr.append("")
        elif funcName == 'Avg':
            if mode[i] >= 1:
                expr.append("x y + 2 /")
            else:
                expr.append("")
        else:
            raise ValueError('_operator2: Unknown \"funcName\" specified!')
    
    return core.std.Expr([clip1, clip2], expr)
################################################################################################################################


################################################################################################################################
## Internal used function for MinFilter() and MaxFilter()
################################################################################################################################
def _min_max_filter(src, flt1, flt2, planes, funcName):
    # Set VS core and function name
    core = vs.get_core()
    
    if not isinstance(src, vs.VideoNode):
        raise TypeError(funcName + ': \"src\" must be a clip!')
    if not isinstance(flt1, vs.VideoNode):
        raise TypeError(funcName + ': \"flt1\" must be a clip!')
    if not isinstance(flt2, vs.VideoNode):
        raise TypeError(funcName + ': \"flt2\" must be a clip!')
    
    # Get properties of input clip
    sFormat = src.format
    if sFormat.id != flt1.format.id or sFormat.id != flt2.format.id:
        raise ValueError(funcName + ': \"src\", \"flt1\" and \"flt2\" must be of the same format!')
    if src.width != flt1.width or src.height != flt1.height or src.width != flt2.width or src.height != flt2.height:
        raise ValueError(funcName + ': \"src\", \"flt1\" and \"flt2\" must be of the same width and height!')
    
    sNumPlanes = sFormat.num_planes
    
    # planes
    process = [0 for i in range(VSMaxPlaneNum)]
    
    if planes is None:
        process = [1 for i in range(VSMaxPlaneNum)]
    elif isinstance(planes, int):
        if planes < 0 or planes >= VSMaxPlaneNum:
            raise ValueError(funcName + ': valid range of \"planes\" is [0, {VSMaxPlaneNum})!'.format(VSMaxPlaneNum=VSMaxPlaneNum))
        process[planes] = 1
    elif isinstance(planes, list):
        for p in planes:
            if not isinstance(p, int):
                raise TypeError(funcName + ': \"planes\" must be a (list of) int!')
            elif p < 0 or p >= VSMaxPlaneNum:
                raise ValueError(funcName + ': valid range of \"planes\" is [0, {VSMaxPlaneNum})!'.format(VSMaxPlaneNum=VSMaxPlaneNum))
            process[p] = 1
    else:
        raise TypeError(funcName + ': \"planes\" must be a (list of) int!')
    
    # Process and output
    expr = []
    for i in range(sNumPlanes):
        if process[i]:
            if funcName == 'MinFilter':
                expr.append("x z - abs x y - abs < z y ?")
            elif funcName == 'MaxFilter':
                expr.append("x z - abs x y - abs > z y ?")
            else:
                raise ValueError('_min_max_filter: Unknown \"funcName\" specified!')
        else:
            expr.append("")
    
    return core.std.Expr([src, flt1, flt2], expr)
################################################################################################################################


################################################################################################################################
## Internal used functions for LimitFilter()
################################################################################################################################
def _limit_filter_expr(defref, thr, elast, largen_thr, value_range):
    flt = " x "
    src = " y "
    ref = " z " if defref else src
    
    dif = " {flt} {src} - ".format(flt=flt, src=src)
    dif_ref = " {flt} {ref} - ".format(flt=flt, ref=ref)
    dif_abs = dif_ref + " abs "
    
    thr = thr * value_range / 255
    largen_thr = largen_thr * value_range / 255
    
    if thr <= 0 and largen_thr <= 0:
        limitExpr = " {src} ".format(src=src)
    elif thr >= value_range and largen_thr >= value_range:
        limitExpr = ""
    else:
        if thr <= 0:
            limitExpr = " {src} ".format(src=src)
        elif thr >= value_range:
            limitExpr = " {flt} ".format(flt=flt)
        elif elast <= 1:
            limitExpr = " {dif_abs} {thr} <= {flt} {src} ? ".format(dif_abs=dif_abs, thr=thr, flt=flt, src=src)
        else:
            thr_1 = thr
            thr_2 = thr * elast
            thr_slope = 1 / (thr_2 - thr_1)
            # final = src + dif * (thr_2 - dif_abs) / (thr_2 - thr_1)
            limitExpr = " {src} {dif} {thr_2} {dif_abs} - * {thr_slope} * + "
            limitExpr = " {dif_abs} {thr_1} <= {flt} {dif_abs} {thr_2} >= {src} " + limitExpr + " ? ? "
            limitExpr = limitExpr.format(dif=dif, dif_abs=dif_abs, thr_1=thr_1, thr_2=thr_2, thr_slope=thr_slope, flt=flt, src=src)
        
        if largen_thr != thr:
            if largen_thr <= 0:
                limitExprLargen = " {src} ".format(src=src)
            elif largen_thr >= value_range:
                limitExprLargen = " {flt} ".format(flt=flt)
            elif elast <= 1:
                limitExprLargen = " {dif_abs} {thr} <= {flt} {src} ? ".format(dif_abs=dif_abs, thr=largen_thr, flt=flt, src=src)
            else:
                thr_1 = largen_thr
                thr_2 = largen_thr * elast
                thr_slope = 1 / (thr_2 - thr_1)
                # final = src + dif * (thr_2 - dif_abs) / (thr_2 - thr_1)
                limitExprLargen = " {src} {dif} {thr_2} {dif_abs} - * {thr_slope} * + "
                limitExprLargen = " {dif_abs} {thr_1} <= {flt} {dif_abs} {thr_2} >= {src} " + limitExprLargen + " ? ? "
                limitExprLargen = limitExprLargen.format(dif=dif, dif_abs=dif_abs, thr_1=thr_1, thr_2=thr_2, thr_slope=thr_slope, flt=flt, src=src)
            limitExpr = " {flt} {ref} > " + limitExprLargen + " " + limitExpr + " ? "
            limitExpr = limitExpr.format(flt=flt, ref=ref)
    
    return limitExpr
################################################################################################################################


################################################################################################################################
## Internal used functions for LimitFilter()
################################################################################################################################
def _limit_diff_lut(diff, thr, elast, largen_thr, planes):
    # Set VS core and function name
    core = vs.get_core()
    funcName = '_limit_diff_lut'
    
    if not isinstance(diff, vs.VideoNode):
        raise TypeError(funcName + ': \"diff\" must be a clip!')
    
    # Get properties of input clip
    sFormat = diff.format
    
    sSType = sFormat.sample_type
    sbitPS = sFormat.bits_per_sample
    
    if sSType == vs.INTEGER:
        neutral = 1 << (sbitPS - 1)
        value_range = (1 << sbitPS) - 1
    else:
        neutral = 0
        value_range = 1
        raise ValueError(funcName + ': \"diff\" must be of integer format!')
    
    # Process
    thr = thr * value_range / 255
    largen_thr = largen_thr * value_range / 255
    '''
    # for std.MergeDiff(src, limitedDiff)
    if thr <= 0 and largen_thr <= 0:
        def limitLut(x):
            return neutral
        return core.std.Lut(diff, planes=planes, function=limitLut)
    elif thr >= value_range / 2 and largen_thr >= value_range / 2:
        return diff
    elif elast <= 1:
        def limitLut(x):
            dif = x - neutral
            dif_abs = abs(dif)
            thr_1 = largen_thr if dif > 0 else thr
            return x if dif_abs <= thr_1 else neutral
        return core.std.Lut(diff, planes=planes, function=limitLut)
    else:
        def limitLut(x):
            dif = x - neutral
            dif_abs = abs(dif)
            thr_1 = largen_thr if dif > 0 else thr
            thr_2 = thr_1 * elast
            thr_slope = 1 / (thr_2 - thr_1)
            
            if dif_abs <= thr_1:
                return x
            elif dif_abs >= thr_2:
                return neutral
            else:
                # final = src - dif * ((dif_abs - thr_1) / (thr_2 - thr_1) - 1)
                return round(dif * (thr_2 - dif_abs) * thr_slope + neutral)
    '''
    # for std.MakeDiff(flt, limitedDiff)
    if thr <= 0 and largen_thr <= 0:
        return diff
    elif thr >= value_range / 2 and largen_thr >= value_range / 2:
        def limitLut(x):
            return neutral
        return core.std.Lut(diff, planes=planes, function=limitLut)
    elif elast <= 1:
        def limitLut(x):
            dif = x - neutral
            dif_abs = abs(dif)
            thr_1 = largen_thr if dif > 0 else thr
            return neutral if dif_abs <= thr_1 else x
        return core.std.Lut(diff, planes=planes, function=limitLut)
    else:
        def limitLut(x):
            dif = x - neutral
            dif_abs = abs(dif)
            thr_1 = largen_thr if dif > 0 else thr
            thr_2 = thr_1 * elast
            
            if dif_abs <= thr_1:
                return neutral
            elif dif_abs >= thr_2:
                return x
            else:
                # final = flt - dif * (dif_abs - thr_1) / (thr_2 - thr_1)
                thr_slope = 1 / (thr_2 - thr_1)
                return round(dif * (dif_abs - thr_1) * thr_slope + neutral)
        return core.std.Lut(diff, planes=planes, function=limitLut)
################################################################################################################################
