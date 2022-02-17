
# 🧋 Smoothie [WIP]

Smoothie is a fork of [blur](https://github.com/f0e/blur) rewritten in Python with the following enhancements:
* Wrote in dead simple Python, code (cli & vpy script) is roughly 150 lines long. The only feature we can't bother to port is the [preview](https://github.com/f0e/blur/blob/master/blur/preview.cpp), which is made in C++
* Static blur config, you won't have to recreate/carry your config around each time you change directory
* Queue multiple files at once via Send To


<details open>
<summary> Simplified configuration, here called a "recipe" ;) </summary>

> Learn what each setting does on it's [wiki page](https://github.com/couleur-tweak-tips/Smoothie/wiki/Configuring-Smoothie-(recipe))

```ini
[interpolation]
enabled=yes
fps=960
speed=medium
tuning=weak
algorithm=23
gpu=yes

[frame blending]
enabled=yes
fps=60
intensity=1.27

[encoding]
process=ffmpeg
args=-c:v libx264 -preset slow -crf 15

[misc]
folder=
deduplication=y
container=mp4
verbose=false
flavors=fruits

[timescale] 
in=1
out=1
```
</details>


## Installation
To install Smoothie and its dependencies for Windows, run this install script:

```powershell
powershell "irm smoothie.ctt.cx|iex"
```
For Linux users and those who seek for a manual installation/already have a Python 3.9/VapourSynth, scroll all the way down

## Using Smoothie
You can simply select one multiple videos and right click and of them -> Send To -> Smoothie

using Smoothie from the command line:

``sm "D:\Video\input1.mp4" "D:\Video\input2.mp4" ...``
    Simply give in the path of the videos you wish to queue to smoothie

``sm myrecipe.ini "D:\Video\input1.mp4" "D:\Video\input2.mp4" ...``
    You can also make the first argument be your custom config file's name, it'll look for it in the settings folder


<details>
<summary>Dependencies </summary>

- [Python](https://www.python.org/downloads) (3.9)
- [FFmpeg](https://ffmpeg.org/download.html)
- [VapourSynth x64](https://www.vapoursynth.com) (R54)

VapourSynth plugins
- [FFMS2](https://github.com/FFMS/ffms2)
- [HAvsFunc](https://github.com/HomeOfVapourSynthEvolution/havsfunc)
- [SVPFlow](https://github.com/bjaan/smoothvideo/blob/main/SVPflow_LastGoodVersions.7z)
- [vs-frameblender](https://github.com/f0e/vs-frameblender)
- [weighting.py](https://github.com/couleur-tweak-tips/Smoothie/blob/master/plugins/weighting.py)
- [filldrops.py](https://github.com/couleur-tweak-tips/Smoothie/blob/master/plugins/filldrops.py)
</details>

