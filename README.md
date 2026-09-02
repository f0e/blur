# Blur

<p align="center">
  <img src="https://github.com/user-attachments/assets/adc158b9-8ec4-4e5a-b372-ed1fad9d8d61" width="30%" />
  <img src="https://github.com/user-attachments/assets/eebbac0d-6fa1-42ed-beeb-e85ea93838b6" width="30%" />
  <img src="https://github.com/user-attachments/assets/e8b749dc-9232-4e45-93b4-8df2e3854ffb" width="30%" />
</p>

[![Downloads](https://img.shields.io/github/downloads/f0e/blur/total?label=Downloads)](https://github.com/f0e/blur/releases/latest) [![Discord](https://img.shields.io/discord/1392389164153962640?style=flat&label=Discord)](https://discord.gg/B5BK9GMN87)

Blur is a native desktop application made for easily and efficiently adding motion blur to videos through frame blending, with the ability to utilise frame interpolation and more.

Join the [Discord](https://discord.gg/B5BK9GMN87) to share your configs, render tests and ask the community for help.

## Download

- [Windows installer](https://github.com/f0e/blur/releases/latest/download/blur-Windows-Installer-x64.exe)
- [macOS installer](https://github.com/f0e/blur/releases/latest/download/blur-macOS-Release-arm64.dmg)
- [Linux (requires manual installation of dependencies)](https://github.com/f0e/blur/releases/latest/download/blur-Linux-Release-x64.tar.gz)

### Beta releases

I often release beta versions with new functionality before I think they're stable enough for a proper release. To test these releases, [visit the Releases tab of the repo.](https://github.com/f0e/blur/releases) To receive beta update notifications you can enable `include beta updates` in your `app.cfg` found in your config folder. Any feedback or issue reporting on these releases is greatly appreciated :)

### macOS notes

> After opening on Mac for the first time you'll get a 'Blur is damaged and can't be opened.' error. To fix this, run `xattr -dr com.apple.quarantine /Applications/blur.app` in Terminal to unquarantine it.\*

> The default interpolation program on macOS is RIFE, unlike Windows and Linux. RIFE is more accurate than SVP, but quite a bit slower. The reason for this difference is because because using SVP for interpolation on macOS requires that [SVP Manager](https://www.svp-team.com/get/) be running, or you'll get a red border around videos. (This software is paid, and not affiliated with Blur)

### Linux notes

Requires manual installation of dependencies. [See here for the list of dependencies.](#linux-dependency-requirements)

## Features

The amount of motion blur is easily configurable, and there are additional options to enable other features such as interpolating the video's fps. This can be used to generate 'fake' motion blur through frame blending the interpolated footage. This motion blur does not blur non-moving parts of the video, like the HUD in gameplay footage.

The program can also be used in the command line via `blur-cli` - see [Command line](#command-line).

## Sample output

### 600fps footage, blurred with 0.6 blur amount

![600fps footage, blurred with 0.6 blur amount](https://i.imgur.com/Hk0XIPe.jpg)

### 60fps footage, interpolated to 600fps, blurred with 0.6 blur amount

![60fps footage, interpolated to 600fps, blurred with 0.6 blur amount](https://i.imgur.com/I4QFWGc.jpg)

As visible from these images, the interpolated 60fps footage produces motion blur that is comparable to actual 600fps footage.

---

## Recommended settings for gameplay footage:

Most of the default settings are what I find work the best, but some settings can depend on your preferences.

### Blur amount

For 60fps footage:

| intent                  | amount  |
| ----------------------- | ------- |
| Maximum blur/smoothness | >1      |
| Normal blur             | 1       |
| Medium blur             | 0.5     |
| Low blur                | 0.2-0.3 |

To preserve your old blur amount when changing framerate use the following formula:

`[new blur amount] = [old blur amount] × ([new fps] / [old fps])`

So normal blur at 30fps becomes 0.5, etc.

### Interpolated fps

Results can become worse if this is too high. In general I recommend around 5x the input fps. Also SVP seems to only be able to interpolate up to 10x the input fps, so don't bother trying anything higher than that.

## Notes

### Limiting smearing

Using blur on 60fps footage results in clean motion blur, but occasionally leaves some smearing artifacts. To remove these artifacts, higher framerate source footage can be used. Recording with software such as OBS at framerates like 120/180fps will result in a greatly reduced amount of artifacting.

### Preventing unsmooth output

If your footage contains duplicate frames then occasionally blurred frames will look out of place, making the video seem unsmooth at points. The 'deduplicate' option will automatically fill in duplicated frames with interpolated frames to prevent this from happening.

### Frameserver output

Blur supports rendering from frameservers. This means you can avoid having to run blur on your input videos when video editing. When rendering, simply output (make sure your project is high framerate) to the frameserver and then drag the generated AVI into blur. Note that some video editing software might limit the maximum project framerate.

## Linux dependency requirements

### General list of things you need

If your distro isn't listed below, here's a list of the things you'll need to install.

- VapourSynth
- FFmpeg
- VapourSynth plugins (install to your system vapoursynth plugin path or [your blur binary directory]/vapoursynth-plugins)
  - [SVPflow](https://web.archive.org/web/20190322064557/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip)
  - [BestSource](https://github.com/vapoursynth/bestsource) ([my automated build](https://github.com/f0e/blur-plugin-builds/releases/latest))
  - [L-SMASH-Works](https://github.com/HomeOfAviSynthPlusEvolution/L-SMASH-Works) - `pip install vapoursynth-lsmas`
  - [MVTools](https://github.com/dubhater/vapoursynth-mvtools) ([my automated build](https://github.com/f0e/blur-plugin-builds/releases/latest))
  - [Akarin](https://github.com/Jaded-Encoding-Thaumaturgy/akarin-vapoursynth-plugin)
  - [RIFE-ncnn-Vulkan](https://github.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/releases/latest)
  - [Adjust](https://github.com/f0e/Vapoursynth-adjust/releases/latest)

### Arch required packages

`paru -S vapoursynth ffmpeg vapoursynth-plugin-svpflow vapoursynth-plugin-bestsource vapoursynth-plugin-mvtools vapoursynth-plugin-rife-ncnn-vulkan`

And manually install [adjust](https://github.com/f0e/Vapoursynth-adjust/releases/latest) and akarin via `pip install vapoursynth-akarin`

---

\*in the future I might buy a dev cert, but $99 a year atm doesn't seem worth it 😅
