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

---

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
  - custom function - generate weights based off of custom python code, which is called for each frame 'x', e.g. -x\*\*2+1

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

- interpolation preset - preset used for framerate interpolation, one of:
  - weak (default) - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - film - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - smooth - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - default _(default svp settings)_
- interpolation algorithm - algorithm used for framerate interpolation, one of:

  - 13 - best overall quality and smoothness (default) - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - 23 - sometimes smoother than 13, but can result in smearing - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - 1 - _[explained further here](https://www.svp-team.com/wiki/Manual:SVPflow)_
  - 2 - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - 11 - _[explained further here](https://www.svp-team.com/wiki/Manual:SVPflow)_
  - 21 - _[explained further here](https://www.svp-team.com/wiki/Manual:SVPflow)_

- interpolation mask area - mask amount used when interpolating. higher values can mean static objects are blurred less, but can also result in less smooth output

### manual svp override

You can customise the SVP interpolation settings even further by manually defining json parameters. [see here](https://www.svp-team.com/wiki/Manual:SVPflow) for explanations on settings

- manual svp: enables manual svp settings, true/false
- super string: json string used as input in [SVSuper](https://www.svp-team.com/wiki/Manual:SVPflow#SVSuper.28source.2C_params_string.29)
- vectors string: json string used as input in [SVAnalyse](https://www.svp-team.com/wiki/Manual:SVPflow#SVAnalyse.28super.2C_params_string.2C_.5Bsrc.5D:_clip.29)
- smooth string: json string used as input in [SVSmoothFps](https://www.svp-team.com/wiki/Manual:SVPflow#SVSmoothFps.28source.2C_super.2C_vectors.2C_params_string.2C_.5Bsar.5D:_float.2C_.5Bmt.5D:_integer.29)

these options are not visible by default, add them to your config and they will be used.

## Recommended settings for gameplay footage:

Most of the default settings are what I find work the best, but some settings can depend on your preferences.

### blur amount

For 60fps footage:

|                             |         |
| --------------------------- | ------- |
| For maximum blur/smoothness | 1       |
| Medium                      | 0.5     |
| Low                         | 0.2-0.3 |

To preserve your old blur amount when changing framerate use the following formula:

`[new blur amount] = [old blur amount] × ([new fps] / [old fps])`

### interpolated fps

Results can become worse if this is too high. In general I recommend around 5x the input fps. Also SVP seems to only be able to interpolate up to 10x the input fps, so don't bother trying anything higher than that.

### Limiting smearing

Using blur on 60fps footage results in clean motion blur, but occasionally leaves some smearing artifacts. To remove these artifacts, higher framerate source footage can be used. Recording with software such as OBS at framerates like 120/180fps will result in a greatly reduced amount of artifacting.

### Preventing unsmooth output

If your footage contains duplicate frames then occasionally blurred frames will look out of place, making the video seem unsmooth at points. The 'deduplicate' option will automatically fill in duplicated frames with interpolated frames to prevent this from happening.

---

## Manual installation

> _Note: I don't suggest manual installation due to the large amount of dependencies. If possible, stick to using the provided installer._

### Requirements

- [Python](https://www.python.org/downloads)
- [FFmpeg](https://ffmpeg.org/download.html)
- [VapourSynth x64](https://www.vapoursynth.com)

### VapourSynth plugins

- [FFMS2](https://github.com/FFMS/ffms2)
- [SVPflow 4.2.0.142](https://web.archive.org/web/20190322064557/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip)
- [vs-frameblender](https://github.com/f0e/vs-frameblender)
- [blur plugins](https://github.com/f0e/blur/tree/master/plugins)

1. Download [the latest release](https://github.com/f0e/blur/releases/latest) or build the project.
2. Install Python
3. Install FFmpeg and [add it to PATH](https://www.wikihow.com/Install-FFmpeg-on-Windows)
4. Install the 64-bit version of VapourSynth
5. Install the required VapourSynth plugins using the command "vsrepo.py install ffms2 havsfunc"
6. Install vs-frameblender manually by downloading the x64 .dll from [here](https://github.com/f0e/vs-frameblender/releases/latest) to "VapourSynth/plugins64"
7. Install SVPflow 4.2.0.142 manually by downloading the zip from [here](https://web.archive.org/web/20190322064557/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip) and moving the files inside "lib-windows/vapoursynth/x64" to "VapourSynth/plugins64"
8. Download the plugins in [/plugins](https://github.com/f0e/blur/tree/master/plugins) to "%appdata%/Roaming/Python/Python39/site-packages"
