from vapoursynth import core
import vapoursynth as vs
from os import path # To split file extension
from configparser import ConfigParser
import havsfunc # aka Interframe2

conf = ConfigParser()
conf.read(config_filepath)

import logging
logging.basicConfig(level=logging.DEBUG)

def defined(var):
    # variable to test must be provided as a string
    # e.g testing the variable var would need to be defined('var'), not defined(var)
    try:
        eval(var)
    except NameError:
        return False
    else:
        return True

def verb(msg):
    if defined('verbose') == True:
        print(logging.debug(f' {msg}'))

# Bool aliases
yes = ['True','true','yes','y','1']
no = ['False','false','no','n','0','null','',None]

if path.splitext(input_video)[1] == '.avi':
    video = core.avisource.AVISource(input_video)
    video = core.fmtc.matrix(clip=video, mat="709", col_fam=vs.YUV, bits=16) # If you're gonna bother with .AVI (let me know of one valid case to use this container) you're probably using 709 anyways
    video = core.fmtc.resampling(clip=video, css="420")
    video = core.fmtc.bitdepth(clip=video, bits=8)
else:
    verb('Starting indexing..')
    video = core.ffms2.Source(source=input_video, cache=False)
    verb(video)

if defined('trim'):

    from datetime import timedelta
    from time import strptime

    def ToSeconds(timestamp):
        if ':' in timestamp:
            timedata = strptime(timestamp,'%M:%S')
            timestamp = timedelta(minutes=timedata.tm_min,seconds=timedata.tm_sec).total_seconds()
        return int(timestamp)

    fps = round(eval(str(video.fps)))
    start, end = (eval(trim)[0]).split(',')
    verb(f'Trimming {start} to {end} with fps {fps}')
    video = core.std.Trim(
        video,
        ToSeconds(start)*fps,
        ToSeconds(end)*fps
    )
verb('calculated trim')
if float(conf['timescale']['in']) != 1: # Input timescale, done before interpolation
    video = core.std.AssumeFPS(video, fpsnum=(video.fps * (1 / float(conf['timescale']['in']))))

# Commented this out as I don't see RIFE being this much better than SVP, and it's not worth the time / long & confusing installation

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

if str(conf['interpolation']['enabled']).lower() in yes: # Interpolation using Interframe2 (uses SVP-Flow, which is also what blur uses)

    if (conf['interpolation']['gpu']) in yes:
        useGPU = True
    elif (conf['interpolation']['gpu']) in no:
        useGPU = False

    if str(conf['interpolation']['fps']).endswith('x'): # if  multiplier support
        interp_fps = int(video.fps * int((conf['interpolation']['fps']).replace('x','')))   
    else:
        interp_fps = int(conf['interpolation']['fps'])

    video = havsfunc.InterFrame(
        video,
        GPU=useGPU,
        NewNum=interp_fps,
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
        if blended_frames % 2 == 0:  # If number is not odd (requires odd number of frames)
            blended_frames += 1
        weights = weighting.equal(blended_frames)
        video = core.frameblender.FrameBlend(video, weights, True)
    video = havsfunc.ChangeFPS(video, int(conf['frame blending']['fps']))

video.set_output()
