from vapoursynth import core
import vapoursynth as vs
from os import path # To split file extension
from configparser import ConfigParser
import havsfunc # aka Interframe2

conf = ConfigParser()
conf.read(config_filepath)

# Bool aliases
yes = ['True','true','yes','y','1']
no = ['False','false','no','n','0','null','',None]

if path.splitext(input_video)[1] == '.avi':
    video = core.avisource.AVISource(f"{input_video}")
    video = core.fmtc.matrix(clip=video, mat="601", col_fam=vs.YUV, bits=16)
    video = core.fmtc.resampling(clip=video, css="420")
    video = core.fmtc.bitdepth(clip=video, bits=8)
else:
    video = core.ffms2.Source(source=input_video, cache=False)

if float(conf['timescale']['in']) != 1: # Input timescale, done before interpolation
    video = core.std.AssumeFPS(video, fpsnum=(video.fps * (1 / float(conf['timescale']['in']))))

#   if str(conf['interpolation']['pre interpolation']['enabled']).lower() in yes: # Pre-interpolating with RIFE
#
#       video = core.resize.Bicubic(video, format=vs.RGBS)
#       if str(conf['interpolation']['pre interpolation']['rife type']).lower() == 'cuda':
#           from vsrife import RIFE
#           while video.fps > conf['interpolation']['pre interpolation']['minimum fps']:
#               video = RIFE(video)
#       elif str(conf['interpolation']['pre interpolation']['rife type']).lower() == 'ncnn':
#           video = core.resize.Bicubic(video, format=vs.RGBS)
#           while video.fps < conf['interpolation']['pre interpolation']['minimum fps']:
#               video = core.rife.RIFE(video)
#       video = core.resize.Bicubic(video, format=vs.YUV420P8, matrix_s="709")

if str(conf['interpolation']['enabled']).lower() in yes: # Interpolation using Interframe2 (uses SVP-Flow, which is what blur uses)

    video = havsfunc.InterFrame(
        video,
        GPU=True,
        NewNum=int(conf['interpolation']['fps']),
        Preset=str(conf['interpolation']['speed']),
        Tuning=str(conf['interpolation']['tuning']),
        OverrideAlgo=int(conf['interpolation']['algorithm'])
    )

if float(conf['timescale']['out']) != 1: # Output timescale, done after interpolation
    video = core.std.AssumeFPS(video, fpsnum=(video.fps * float(conf['timescale']['out'])))

if str(conf['misc']['deduplication']).lower() in yes:
    import filldrops
    video = filldrops.FillDrops(video, thresh=0.001)

if str(conf['frame blending']['enabled']).lower() in yes:
    import weighting
    frame_gap = int(video.fps / int(conf['frame blending']['fps']))
    blended_frames = int(frame_gap * float(conf['frame blending']['intensity']))
    if blended_frames > 0:
        if blended_frames % 2 == 0:
            blended_frames += 1
        weights = weighting.equal(blended_frames)
        video = core.frameblender.FrameBlend(video, weights, True)
    video = havsfunc.ChangeFPS(video, int(conf['frame blending']['fps']))

video.set_output()
