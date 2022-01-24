import os
Temp=os.environ["TEMP"]

# Resample
    
def Resample(Video, Intensity, FPS):
    Resample = open(Temp+"/Render.vpy", "w+")
    Intensity=float(Intensity)
    Script=f""" 
from vapoursynth import core
import vapoursynth as vs
import havsfunc as haf
import adjust
video = core.ffms2.Source(source=r"{Video}", cache=False)
frame_gap = int(video.fps / 60)
blended_frames = int(frame_gap * {Intensity})
if blended_frames > 0:
	if blended_frames % 2 == 0:
		blended_frames += 1
	weights = [1 / blended_frames] * blended_frames
	video = core.frameblender.FrameBlend(video, weights, True)
video = haf.ChangeFPS(video, {FPS})
video.set_output()
"""
    Resample.write(Script)
    Resample.close()

# Broken
def LegacyInterpolate(Video,FPS,Preset,Tuning,Algorithm):
    Interpolate = open(Temp+"/Render.vpy", "w+")
    Script=f"""
from vapoursynth import core
import vapoursynth as vs
import havsfunc as haf
import adjust
video = core.ffms2.Source(source=r"{Video}", cache=False)
video = haf.InterFrame(video, GPU=True, NewNum={FPS}, Preset="{Preset}", Tuning="{Tuning}", OverrideAlgo={Algorithm})
video.set_output()
"""
    Interpolate.write(Script)
    Interpolate.close()

# Interpolate + Blur
def Interpolate(Video, InterpolateFPS, Preset, Tuning, Algorithm, ResampleFPS, Intensity):
    Interpolate = open(Temp+"/Render.vpy", "w+")
    Script=f"""
from vapoursynth import core
import vapoursynth as vs
import havsfunc as haf
import adjust
video = core.ffms2.Source(source=r"{Video}", cache=False)
video = haf.InterFrame(video, GPU=True, NewNum={InterpolateFPS}, Preset="{Preset}", Tuning="{Tuning}", OverrideAlgo={Algorithm})
frame_gap = int(video.fps / 60)
blended_frames = int(frame_gap * {Intensity})
if blended_frames > 0:
	if blended_frames % 2 == 0:
		blended_frames += 1
	weights = [1 / blended_frames] * blended_frames
	video = core.frameblender.FrameBlend(video, weights, True)
video = haf.ChangeFPS(video, {ResampleFPS})
video.set_output()
""" 
    Interpolate.write(Script)
    Interpolate.close()     