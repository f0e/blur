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

## Sample config file
```c
cpu cores: 4

input fps: 60
output fps: 60

input timescale: 1
output timescale: 1

blur: true
blur amount: 0.6

interpolate: true
interpolated fps: 600

preview: true

crf: 18
gpu: false
detailed filenames: false

interpolation speed: default
interpolation tuning: default
interpolation algorithm: default
```

## Requirements
- [FFmpeg](https://ffmpeg.org/download.html)
- [AviSynth+ (x64)](https://avs-plus.net/)

### AviSynth plugins
- [ZIP containing all of the required plugin files (recommended)](https://cdn.discordapp.com/attachments/588965578493001729/836700121772064858/AviSynth_plugins.7z)

or

- [FFmpegSource](https://github.com/FFMS/ffms2/releases/latest)
- [InterFrame (ignore the dependencies)](https://www.spirton.com/interframe-2-8-2-released/)
- [SVPflow 4.2.0.142](https://web.archive.org/web/20190322064557/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip)
- [ClipBlend](http://avisynth.nl/index.php/ClipBlend)

## Installation
1. Download [the latest release](https://github.com/f0e/blur/releases/latest) or build the project.
2. Install FFmpeg and [add it to PATH](https://www.wikihow.com/Install-FFmpeg-on-Windows)
3. Install the 64-bit version of AviSynth+
4. Install the required AviSynth plugins into the plugins64+ directory inside the AviSynth+ installation folder

## Usage
1. Open the executable and drag a video file onto the console window, or directly drop video files onto the executable file.
2. A config file will be generated in the video's directory, which can be modified to suit your needs.
3. The program will process the inputted video according to the configuration file located in the same folder as the video, and will output the blurred version to the same directory with " - blur" appended.

***

## Config settings explained:
- cpu cores - amount of cpu cores you have

- input fps - input video file fps
- output fps - final output video file fps

- input timescale - timescale of the input video file (will be sped up/slowed down accordingly)
- output timescale - timescale of the output video file

- blur - whether or not the output video file will have motion blur
- blur amount - if blur is enabled, this is the amount of motion blur from 0-1

- interpolate - whether or not the input video file will be interpolated to a higher fps
- interpolated fps - if interpolate is enabled, this is the fps that the input file will be interpolated to (before blending)

- crf - [crf](https://trac.ffmpeg.org/wiki/Encode/H.264#crf) of the output video
- gpu - experimental gpu acceleration (doesn't work)
- detailed filenames - adds blur settings to generated filenames

- interpolation speed - default is 'medium', [explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)
- interpolation tuning - default is 'smooth', [explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)
- interpolation algorithm - default is 13, [explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)

## Recommended settings for gameplay footage:
### Config options
- blur amount - for maximum blur/smoothness use 1, for medium blur use 0.5. 0.6-0.8 gives nice results

- interpolated fps - results become worse if this is too high, for 60fps source footage around 300-900 should be good, for 180fps 1200 is good

- interpolation speed - just keep it at default
- interpolation tuning - for gameplay footage default (smooth) keeps the crosshair intact, but film is also a good option
- interpolation algorithm - just keep it at default

### Limiting smearing
Using blur on 60fps footage results in clean motion blur, but occasionally leaves some smearing artifacts. To remove these artifacts, higher framerate source footage can be used. Recording with software such as OBS at framerates like 120/180fps will result in a greatly reduced amount of artifacting.
