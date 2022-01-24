
# 🧋 Smoothie [WIP; yet has to be released]

Smoothie is a fork of [blur](https://github.com/f0e/blur) rewrote in Python with the following enhancements:
* Static blur config, you won't have to recreate/move your config each time you change a directory
* Queue multiple files at once via Send To
* Option to pipe to Av1an (instead of ffmpeg)

## Notes about this Smoothie Fork:
This is a somewhat proof of concept working version of Smoothie but isn't meant to be the final project.

### Usage
```
Smoothie -edit <Edit Smoothie's Config File>
Smoothie -resample Video1.mp4 Video2.mp4 Video3.mp4 <Resample the specified videos.>
Smoothie -interpolate Video1.mp4 Video2.mp4 Video3.mp4 <Interpolate and resample the specified videos.>
```

### Config File Explained.
```ini
[interpolation]
fps = 480
speed = medium
tuning = weak
algorithm = 23

[resampling]
fps = 60
intensity = 1

[rendering]
process = ffmpeg.exe
arguments = -y -hwaccel cuda -threads 8 -loglevel error -hide_banner -stats -i {Input} -c:v hevc_nvenc -rc constqp -preset p7 -qp 18 {Output}
```
#### Interpolation

1. `fps` = Interpolate FPS
2. `speed` = Interpolation Speed
3. `tuning` = Interpolation Tuning
4. `algorithm` = Interpolation Algorithm

#### Resampling

1. `fps` = Resample FPS
2. `Intensity` = Resample Intensity

#### Rendering

1. `process` = Pipe/Rendering Program.
2. `arguments` = Pipe Arguments/Rendering Program Arguments.         
Variables:      
`{Input}` = Video Input.     
`{Output}` = Video Output.         

#### Important!
1. **This Smoothie build uses blur's equal weighting.**
2. **SVP is the default interpolation program.**

### Compile
1. Install Python `3.9`.
2. Install using `pip`.
```
pip install nuitka
pip install zstandard
pip install wmit
```
3. Run `build.bat`.

### Requirements
- [Python](https://www.python.org/downloads) (3.9)
- [FFmpeg](https://ffmpeg.org/download.html)
- [VapourSynth x64](https://www.vapoursynth.com)

### VapourSynth plugins
- [FFMS2](https://github.com/FFMS/ffms2)
- [HAvsFunc](https://github.com/HomeOfVapourSynthEvolution/havsfunc)
- [SVPflow 4.2.0.142](https://web.archive.org/web/20190322064557/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip)
- [vs-frameblender](https://github.com/f0e/vs-frameblender)
- [weighting.py](https://github.com/couleur-tweak-tips/Smoothie/blob/master/plugins/weighting.py)
- [filldrops.py](https://github.com/couleur-tweak-tips/Smoothie/blob/master/plugins/filldrops.py)
