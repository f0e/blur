
# 🧋 Smoothie [WIP]

Smoothie is a fork of [blur](https://github.com/f0e/blur) rewrote in Python with the following enhancements:
* Static blur config, you won't have to recreate/move your config each time you change a directory
* Queue multiple files at once via Send To
* Option to pipe to ffmpeg or Av1an

## Installation
To install Smoothie for Windows, use our [Scoop](https://github.com/ScoopInstaller/Scoop) bucket:

```powershell
Set-ExecutionPolicy Bypass -Scope Process -Force ; Invoke-RestMethod https://get.scoop.sh | Invoke-Expression
```

Then you can add utils' bucket and install Smoothie:
```
scoop.cmd install utils/smoothie
```

If you already have your own VapourSynth/Python environment installed you can clone the repository and make your own shortcuts, also see [dependencies](#dependencies)

For manual installation, see [manual installation](#manual-installation)

## Usages
1. Select one multiple videos and right click and of them -> Send To -> Smoothie
2. Open up your favorite terminal and type in smoothie \<inputvideo>
The program will process the inputted video according to the configuration file used, and will save the output video to the same directory, or a custom one if you specify it in Smoothie's config.yaml file

Use -h or --help for more information about using Smoothie through the command line.

***

## Config settings explained:
### Resample
- enabled - whether or not the output video file will have motion blur
- amount - if resample is enabled, this is the amount of motion blur (0 = no blur, 1 = fully blend every frame together, 1+ = more blur/ghosting)
- output fps - if blur is enabled, this is the fps the output video will be
### interpolation
- interpolate - whether or not the input video file will be interpolated to a higher fps
- interpolated fps - if interpolate is enabled, this is the fps that the input file will be interpolated to (before blending)
- interpolation speed - default is 'medium'
- interpolation tuning - default is 'weak'
- interpolation algorithm - default is 23
  [Interpolation settings are explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)
  - interpolation program (svp/rife/rife-ncnn) - program used for interpolation.
  - svp - fastest option, also blurs static parts of video the least
  - rife - considerably slower than SVP but can produce more accurate results, particularly for low framerate input videos. this is the CUDA implementation of RIFE, and is the faster option for NVIDIA gpus.
  - rife-ncnn - Vulkan implementation of rife, works for all devices but is slower.

### timescale
- input timescale - timescale of the input video file (will be sped up/slowed down accordingly)
- output timescale - timescale of the output video file
- adjust timescaled audio pitch - will pitch shift audio when sped up/slowed down

### rendering
- gpu - enables gpu accelerated rendering (likely slower)
- gpu type (nvidia/amd/intel) - your gpu's type (will be parsed if left default)
- deduplicate - removes duplicate frames and generates new interpolated frames to take their place (using ``filldrops.py``)
- custom ffmpeg filters - custom ffmpeg flags (video filters and encoding args)
  
  ### color grading
- brightness - brightness of the output video
- saturation - saturation of the output video
- contrast - contrast of the output video

## Recommended settings for gameplay footage:
### Config options
- blur amount - the higher fps you render in, the higher you can set this value without it looking too blurry. Nothing higher than 1.0 at 30FPS, you can go up to 1.75 at 60FPS and for 120FPS you can try 3.0 to 5.0
- interpolated fps - to make good frames with SVP you'll need your input FPS clip to have at the very least 120FPS
- interpolation settings - defaults

### Limiting smearing
Using blur on 60fps footage results in clean motion blur, but occasionally leaves some smearing artifacts. To remove these artifacts, higher framerate source footage can be used. Recording with software such as OBS at framerates like 120/180fps will result in a greatly reduced amount of artifacting.

### Preventing unsmooth output
If your footage contains duplicate frames then occasionally blurred frames will look out of place, making the video seem unsmooth at points. The 'deduplicate' option will automatically fill in duplicated frames with interpolated frames to prevent this from happening. This will only affect micro stutters, don't expect this to magically recover your clips, but will smoothen up if you had a little bit of encoding lag on OBS (e.g at ~0.5%)

## Manual installation
Note: I don't suggest manual installation due to the large amount of dependencies. If possible, stick to using the provided installer.

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

### Manual installation steps

1. Install Python (<=3.9)
2. Install FFmpeg and [add it to PATH](https://www.wikihow.com/Install-FFmpeg-on-Windows)
3. Install the 64-bit version of VapourSynth
4. Install the required VapourSynth plugins using the command "vsrepo.py install ffms2 havsfunc"
5. Install vs-frameblender manually by downloading the x64 .dll from [here](https://github.com/f0e/vs-frameblender/releases/latest) to "VapourSynth/plugins64"
6. Install SVPflow 4.2.0.142 manually by downloading the zip from [here](https://web.archive.org/web/20190322064557/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip) and moving the files inside "lib-windows/vapoursynth/x64" to "VapourSynth/plugins64"
7. Install [weighting.py](https://raw.githubusercontent.com/couleur-tweak-tips/smoothie/master/plugins/weighting.py) and [filldrops.py](https://github.com/couleur-tweak-tips/smoothie/blob/master/plugins/filldrops.py) to "%appdata%/Python/Python39/site-packages"
