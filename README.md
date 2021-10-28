# Blur
Blur is a program made for easily and efficiently adding motion blur to videos through frame blending.

## Features
The amount of motion blur is easily configurable, and there are additional options to enable other features such as interpolating the video's fps. This can be used to generate 'fake' motion blur through frame blending the interpolated footage. This motion blur does not blur non-moving parts of the video, like the HUD in gameplay footage.

## Sample output
### 600fps footage, blurred with 0.6 blur amount
![](https://i.imgur.com/Hk0XIPe.jpg)
### 60fps footage, interpolated to 600fps, blurred with 0.6 blur amount
![](https://i.imgur.com/I4QFWGc.jpg)

As visible from these images, the interpolated 60fps footage produces motion blur that is comparable to actual 600fps footage.

## Installation
To install blur for Windows, just download and run [the installer](https://github.com/f0e/blur/releases/latest). Other operating systems are not currently supported.

For manual installation, see [manual installation](#manual-installation)

## Usage
1. Open the executable and drag a video file onto the console window, or directly drop video files onto the executable file.
2. A config file will be generated in the video's directory, which can be modified to suit your needs.
3. The program will process the inputted video according to the configuration file located in the same folder as the video, and will output the blurred version to the same directory with " - blur" appended.

The program can also be used in the command line, use -h or --help for more information.

***

## Config settings explained:
### blur
- blur - whether or not the output video file will have motion blur
- blur amount - if blur is enabled, this is the amount of motion blur (0 = no blur, 1 = fully blend every frame together, 1+ = more blur/ghosting)
- blur output fps - if blur is enabled, this is the fps the output video will be
- blur weighting - weighting function to use when blending frames. options are listed below:
  - equal - each frame is blended equally
  - gaussian
  - gaussian_sym
  - pyramid
  - pyramid_sym
  - custom weights - custom frame weights, e.g. [5, 3, 3, 2, 1]. higher numbers indicate frames being more visible when blending, lower numbers mean they are less so.
  - custom function - generate weights based off of custom python code, which is called for each frame 'x', e.g. -x**2+1

### interpolation
- interpolate - whether or not the input video file will be interpolated to a higher fps
- interpolated fps - if interpolate is enabled, this is the fps that the input file will be interpolated to (before blending)

### rendering
- quality - [crf](https://trac.ffmpeg.org/wiki/Encode/H.264#crf) of the output video (qp if using GPU rendering)
- preview - opens a render preview window
- detailed filenames - adds blur settings to generated filenames

### timescale
- input timescale - timescale of the input video file (will be sped up/slowed down accordingly)
- output timescale - timescale of the output video file
- adjust timescaled audio pitch - will pitch shift audio when sped up/slowed down

### filters
- brightness - brightness of the output video
- saturation - saturation of the output video
- contrast - contrast of the output video

### advanced rendering
- gpu - enables experimental gpu accelerated rendering (likely slower)
- gpu type (nvidia/amd/intel) - your gpu type
- deduplicate - removes duplicate frames and generates new interpolated frames to take their place
- custom ffmpeg filters - custom ffmpeg filters to be used when rendering (replaces gpu & quality options)

### advanced blur
- blur weighting gaussian std dev - standard deviation used in the gaussian weighting
- blur weighting triangle reverse - reverses the direction of the triangle weighting
- blur weighting bound - weighting bounds, spreads out weights more

### advanced interpolation
- interpolation program (svp/rife/rife-ncnn) - program used for interpolation.
  - svp - fastest option, also blurs static parts of video the least
  - rife - considerably slower than SVP but can produce more accurate results, particularly for low framerate input videos. this is the CUDA implementation of RIFE, and is the faster option for NVIDIA gpus.
  - rife-ncnn - Vulkan implementation of rife, works for all devices but is slower.
- interpolation speed - default is 'medium', [explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)
- interpolation tuning - default is 'smooth', [explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)
- interpolation algorithm - default is 13, [explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)

## Recommended settings for gameplay footage:
### Config options
- blur amount - for maximum blur/smoothness use 1, for medium blur use 0.5, low blur 0.2-0.3. 0.6-0.8 gives nice results for 60fps, 0.3~ is good for 30fps
- blur weighting - just keep it at equal

- interpolated fps - results become worse if this is too high, for 60fps source footage around 300-900 should be good, for 180fps 1200 is good. In general the limit tends to be at around 10x the input video's fps.

- interpolation speed - just keep it at default
- interpolation tuning - for gameplay footage default (smooth) keeps the crosshair intact, but film is also a good option
- interpolation algorithm - just keep it at default

### Limiting smearing
Using blur on 60fps footage results in clean motion blur, but occasionally leaves some smearing artifacts. To remove these artifacts, higher framerate source footage can be used. Recording with software such as OBS at framerates like 120/180fps will result in a greatly reduced amount of artifacting.

### Preventing unsmooth output
If your footage contains duplicate frames then occasionally blurred frames will look out of place, making the video seem unsmooth at points. The 'deduplicate' option will automatically fill in duplicated frames with interpolated frames to prevent this from happening.

## Manual installation
Note: I don't suggest manual installation due to the large amount of dependencies. If possible, stick to using the provided installer.

### Requirements
- [Python](https://www.python.org/downloads)
- [FFmpeg](https://ffmpeg.org/download.html)
- [VapourSynth x64](https://www.vapoursynth.com)

### VapourSynth plugins
- [FFMS2](https://github.com/FFMS/ffms2)
- [HAvsFunc](https://github.com/HomeOfVapourSynthEvolution/havsfunc)
- [SVPflow 4.2.0.142](https://web.archive.org/web/20190322064557/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip)
- [vs-frameblender](https://github.com/f0e/vs-frameblender)
- [weighting.py](https://github.com/f0e/blur/blob/master/plugins/weighting.py)
- [filldrops.py](https://github.com/f0e/blur/blob/master/plugins/filldrops.py)

1. Download [the latest release](https://github.com/f0e/blur/releases/latest) or build the project.
2. Download and run [installer.bat](https://raw.githubusercontent.com/f0e/blur/master/installer.bat) to automatically install all of the requirements.

Or

2. Install Python
3. Install FFmpeg and [add it to PATH](https://www.wikihow.com/Install-FFmpeg-on-Windows)
4. Install the 64-bit version of VapourSynth
5. Install the required VapourSynth plugins using the command "vsrepo.py install ffms2 havsfunc"
6. Install vs-frameblender manually by downloading the x64 .dll from [here](https://github.com/f0e/vs-frameblender/releases/latest) to "VapourSynth/plugins64"
7. Install SVPflow 4.2.0.142 manually by downloading the zip from [here](https://web.archive.org/web/20190322064557/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip) and moving the files inside "lib-windows/vapoursynth/x64" to "VapourSynth/plugins64"
8. Install [weighting.py](https://raw.githubusercontent.com/f0e/blur/master/plugins/weighting.py) and [filldrops.py](https://github.com/f0e/blur/blob/master/plugins/filldrops.py) to "%appdata%/Roaming/Python/Python39/site-packages"
