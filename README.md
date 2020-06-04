# Blur
Blur is a program made for easily and efficiently adding motion blur to videos through frame blending.

## Features
The amount of motion blur is easily configurable, and there are additional options to enable other features such as interpolating the video's fps. This can be used to generate 'fake' motion blur through frame blending the interpolated footage. This motion blur does not blur non-moving parts of the video, like the HUD in gameplay footage.

## Sample output
### 600fps footage, blurred with 0.6 exposure
![](https://i.imgur.com/Hk0XIPe.jpg)
### 60fps footage, interpolated to 600fps, blurred with 0.6 exposure
![](https://i.imgur.com/I4QFWGc.jpg)

## Sample config file
```c
cpu_cores: 8
cpu_threads: 16

input_fps: 60
output_fps: 30

timescale: 1

blur: true
exposure: 0.6

interpolate: true
interpolated fps: 480
```

## Requirements
- [FFmpeg](https://ffmpeg.zeranoe.com/builds/)
- [AviSynth+ (x64)](https://avs-plus.net/)

### AviSynth plugins
- [FFmpegSource](https://github.com/FFMS/ffms2/releases/latest)
- [InterFrame](https://www.spirton.com/interframe-2-8-2-released/)

## Installation
1. Download [the latest release](https://github.com/f0e/blur/releases/latest) or build the project.
2. Install FFmpeg and [add it to PATH](https://video.stackexchange.com/a/20496)
3. Install the 64-bit version of AviSynth+
4. Install each of the required AviSynth plugins into the plugins64+ directory inside the AviSynth+ installation folder

## Usage
Drag video files onto the program executable. A config file will be generated in the video's directory, which can be modified to suit your needs.