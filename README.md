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

When interpolation is on it does both jobs in one pass: every frame it generates comes from the two nearest frames that were really captured, rather than from frames that were themselves generated to patch a gap.

### Configs

A config is a full set of blur settings. They're kept one to a file in the `configs` folder of your config folder, named after the file - `configs/slowmo.cfg` is the config called `slowmo` - so a config can be copied between machines, or edited by hand, on its own.

One of them is the default, which is what videos start on when you add them unless a [rule](#rules) picks another. Create, rename and delete configs from the dropdown at the top of the blur settings tab, and set which one is the default there too - the star marks it, and clicking the lit star unsets it again.

Each video in the queue can be switched to a different config from its own dropdown, so a batch can mix them - a `slowmo` clip and a normal one rendered in one sitting. Switching a video's config also resets its mask options to whatever that config asks for.

With no default set and no rule matching, videos are added to the queue without a config and their dropdown reads `select a config` until you pick one. Rendering then starts the videos that have a config and leaves the rest in the queue, telling you how many are still waiting on one.

Edits to every config are kept while the settings screen is open, so you can flick between them and save the lot in one go.

On the command line, `--config-name` says which config each video renders with, one per input - see [Command line](#command-line).

### Rules

A rule points every video whose path matches a pattern at a config, so clips out of a folder land on the right settings without picking one each time. They live in the `rules` tab of the settings screen, next to the preview, and in `rules.cfg` in your config folder.

Each rule is a pattern and the config it picks. `*` matches any run of characters and `?` matches a single one, and a pattern with neither is matched anywhere in the path - so `valorant` catches anything with that in its path, `D:/clips/apex/*` catches a folder, and `*.mkv` catches an extension. Matching ignores case, and treats `\` and `/` the same, so a pattern copied off a Windows path works on Linux too.

Rules are ordered and the first enabled one that matches wins, so put the specific ones above the general ones. Drag a rule by its handle to move it.

A video picks its config when it's added, in this order:

1. a config chosen for that video, or `--config-name` on the command line
2. the first enabled rule that matches its path
3. the default config

The queue says which of those it was underneath the config dropdown, and changing the config there overrides it for that video either way.

Deleting a config leaves any rules pointing at it in place, flagged, and they're skipped until a config of that name exists again. Renaming one takes its rules with it.

### Masks

Interpolation has no idea that a HUD isn't part of the scene, so it warps static overlays - crosshairs, killfeeds, timers - along with the world behind them. A mask marks those regions so they keep their original pixels instead. It applies to deduplication as well, since filling in a dropped frame means interpolating one, and that warps an overlay the same way.

Masks are image files kept in the `masks` folder of your config folder - png, jpg, bmp, webp and tiff all work. White means "interpolate this as normal", black means "leave this alone". Anything in between blends the two.

Pick one with the `mask` config setting to apply it to everything by default, and override it per clip from the dropdown in the queue - so a Counter-Strike mask and a Fortnite mask can be used on different clips in the same batch. `blur-cli` takes a `--mask` argument for the same thing (`--mask none` turns off a mask set in the config).

Two things worth knowing:

- Masked areas still get motion blur. Only interpolation skips them, so they blend like the rest of the video.
- The mask is scaled to fit the video, so one mask works across resolutions as long as the aspect ratio matches.

#### Automatic masks

Turning on `auto mask` works a mask out from the video itself, so you don't have to draw one per game. It's a separate setting from `mask`, and the two stack - a pixel is protected if either of them protects it.

It samples frames from across the whole video and looks for the pixels that stayed put in nearly all of them - either because they were exactly the same colour every time, which is what a solid overlay looks like, or because they kept sitting on the same side of their surroundings, which is what a see-through one like a crosshair does when the scene behind it keeps changing. Both brightness and colour are checked, so a green crosshair is still found on ground it happens to match in brightness. The scene moves as the camera does; a crosshair, HUD, scoreboard or watermark doesn't.

The mask is kept tight to what it found, with only a pixel or two of margin. That matters more than it sounds: masked pixels come from the un-interpolated video, so mask that spills onto the scene freezes a halo of background around each overlay, and a frozen halo in the middle of smooth motion is more noticeable than the interpolation artifact the margin would have covered.

Because it works off what doesn't change, it finds overlays that are always on screen and always in the same place. Parts that animate - a killfeed appearing and disappearing, say - move like the rest of the frame, and faint text that barely stands out from what's behind it can be missed too. It also needs the scene itself to move: if the video doesn't change enough for the overlay to stand out against it, or so much of the frame is static that it can't be an overlay, the video is rendered without a mask.

If you've trimmed a clip, only the trimmed part is analysed - a different section of a video can have a different overlay on it, so the mask is worked out from what you're actually rendering.

It costs a couple of dozen extra frame decodes per video, once, before rendering starts - a few seconds for a 1440p clip. The result is then kept in the `auto-masks` folder of your config folder and reused, so a video is only analysed once instead of again for every preview you scrub through. It's keyed on the video, on the part of it being rendered, and on how the analysis works, so trimming differently, editing the video or updating blur each get you a fresh one. They're pngs of a few kilobytes each, and the 256 most recent are kept.

Those are ordinary mask images, so if one comes out nearly right you can copy it into the `masks` folder, touch it up in whatever you like, and use it as a normal mask on other clips of the same game.

### Frameserver output

Blur supports rendering from frameservers. This means you can avoid having to run blur on your input videos when video editing. When rendering, simply output (make sure your project is high framerate) to the frameserver and then drag the generated AVI into blur. Note that some video editing software might limit the maximum project framerate.

## Command line

`blur-cli` renders without opening the app, using the same configs, rules and masks. `blur-cli --help` lists everything; the options are:

| Option | What it does |
| --- | --- |
| `-i, --input` | Input file(s). Required, and repeatable. |
| `-o, --output` | Output file(s). One per input if given, otherwise names are worked out from the input. |
| `--config-name` | Name of a config in the `configs` folder, one per input. Takes precedence over `--config-path`. |
| `-c, --config-path` | Path to a `.cfg` anywhere on disk, one per input, for a config that isn't in the folder. |
| `--mask` | Mask image filename in the `masks` folder, or `none` to turn off a mask the config sets. |
| `--auto-mask`, `--no-auto-mask` | Turn the generated mask on or off for this run. Left off entirely, the config decides. |
| `-p, --preview` | Show a preview while rendering. |
| `-v, --verbose` | Log more. |

```
blur-cli -i clip.mp4
blur-cli -i clip.mp4 -o blurred.mp4 --config-name slowmo
blur-cli -i a.mp4 -i b.mp4 --config-name slowmo --config-name default
blur-cli -i clip.mp4 --mask none --no-auto-mask
```

With no `--config-name` or `--config-path`, each input gets the config a rule picks for its path, or the default config. If none of those resolve for an input it's skipped with a message, and if none of them could resolve for any input `blur-cli` says so and stops before rendering anything.

## Config settings explained:

### blur

- blur - whether or not the output video file will have motion blur
- blur amount - if blur is enabled, this is the amount of motion blur (0 = no blur, 1 = fully blend every frame together, 1+ = more blur/ghosting)
- blur output fps - if blur is enabled, this is the fps the output video will be. can be a framerate (e.g. 600) or a multiplier (e.g. 5x)
- blur weighting - weighting function to use when blending frames. options are listed below. also: [view weighting comparison graphs.](tests/plot_weighting_functions/weighting_functions.pdf)
  - equal - each frame is blended equally
  - gaussian_sym
  - vegas
  - pyramid
  - gaussian
  - ascending
  - descending
  - gaussian_reverse
  - custom weights - custom comma-separated frame weights, e.g. 5, 3, 3, 2, 1. higher numbers indicate frames being more visible when blending, lower numbers mean they are less so.

### interpolation

- interpolate - whether or not the input video file will be interpolated to a higher fps
- interpolated fps - if interpolate is enabled, this is the fps that the input file will be interpolated to (before blurring). can be a set fps number or a multiplier (append x to end e.g. `5x`)
- interpolation method - method used for interpolation:
  - Quality: RIFE > svp
  - Speed: svp > RIFE
  - Note: On macOS, SVP requires SVP Manager to be open or a red border will appear. It provides a 30-day trial, but then costs $24.99 for a lifetime license. RIFE can always be used however, but it is slower than SVP.

### pre-interpolation

- pre-interpolation - enable pre-interpolation using a more accurate but slower AI model before main interpolation
- pre-interpolated fps - FPS to pre-interpolate input video to (before blurring). can be a set fps number or a multiplier (append x to end e.g. `5x`)

### masking

- mask - mask image used to protect parts of the frame from interpolation and deduplication, `auto` to work one out from the video, or `none`. [see masks](#masks)

### rendering

- quality - [crf](https://trac.ffmpeg.org/wiki/Encode/H.264#crf) of the output video (may be different if using GPU encoding) - (0 = lossless quality, 51 = really bad)
- deduplicate - ignores duplicate frames and generates what should have been there instead, from the nearest frames that aren't repeats. fixes 'unsmooth' looking output caused by stuttering in recordings
- deduplicate range - how far apart two frames can be and still have frames generated between them. make it higher if your footage is at a lower FPS than it should be (e.g. choppy 120fps gameplay recorded at 240fps), lower it if your blurred footage starts blurring static elements such as menu screens
- deduplicate real frame - which frame of a run of duplicates is the one that was really drawn. nothing in the file records it and the picture can't tell you, so it's a choice:
  - `first` - what a live recording does: the picture is drawn, then held until the next is ready. almost always right
  - `last` - the run ends on the real frame instead, which is what a variable framerate recording resampled to a fixed one can look like
  - `center` - split the difference. never more than half a run out whichever way the footage leans, where picking the wrong end can be a whole run out
  - `surrounding` - don't believe the run at all, work from the frames either side of it. comes out right whichever way the footage leans, but needs runs of one frame to work from (so it suits stuttery footage, not a game running at a clean half of the recording framerate) and generates across a longer gap. raise `deduplicate range` to give it room
- deduplicate max future checks - how many times `surrounding` may step over a run that is itself in question, looking for a frame whose timing isn't. each step widens the gap it generates across
- deduplicate threshold - threshold of movement that triggers deduplication. turn on debug in advanced and render a video to label every frame deduplication had a hand in with the movement it measured and the frames it worked from (turn blur off to read it - blending averages the text away along with everything else)
- deduplicate method - what generates the frames that go in place of duplicates. only needed with interpolation off - with it on, the interpolation method generates them as part of its own pass, from the same model. options:
  - Quality: RIFE > svp
  - Speed: old > svp > RIFE
- preview - opens a render preview window
- detailed filenames - adds blur settings to generated filenames
- copy dates - copies over the modified date from the input file to the output file

### gpu acceleration

- gpu decoding - uses gpu when decoding
- gpu interpolation - uses gpu when interpolating
- gpu encoding - uses gpu when rendering
- gpu type (nvidia/amd/intel) - your gpu type

### timescale

- input timescale - timescale of the input video file (will be sped up/slowed down accordingly)
- output timescale - timescale of the output video file
- adjust timescaled audio pitch - will pitch shift audio when sped up/slowed down

### filters

- brightness - brightness of the output video
- saturation - saturation of the output video
- contrast - contrast of the output video

### advanced rendering

- video container - the output video container format (e.g. `mp4`, `mkv`, `avi`)
- custom ffmpeg filters - custom ffmpeg filters to be used when rendering (replaces gpu & quality options)
- debug - shows debug window, prints commands used by blur
- source plugin - what decodes the input video, one of:
  - `LWLibavSource` (default) - L-SMASH-Works, generally faster
  - `BestSource` - slower, but more accurate seeking. also used as a fallback when L-SMASH-Works isn't available

### advanced blur

- blur weighting gaussian std dev - standard deviation used in the gaussian weighting
- blur weighting gaussian mean - mean used in the gaussian weighting
- blur weighting gaussian bound - bound used in the gaussian weighting

### advanced interpolation

- SVP interpolation preset - preset used for framerate interpolation when using SVP, one of:
  - weak (default) - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - film - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - smooth - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - default _(default svp settings)_
- SVP interpolation algorithm - algorithm used for framerate interpolation when using SVP, one of:
  - 13 - best overall quality and smoothness (default) - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - 23 - sometimes smoother than 13, but can result in smearing - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - 1 - _[explained further here](https://www.svp-team.com/wiki/Manual:SVPflow)_
  - 2 - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - 11 - _[explained further here](https://www.svp-team.com/wiki/Manual:SVPflow)_
  - 21 - _[explained further here](https://www.svp-team.com/wiki/Manual:SVPflow)_

- interpolation block size - block size used for framerate interpolation. higher block size = less accurate blur, will result in spaces around non-moving objects of the frame, also renders faster. lower block size = more accurate blur, but can result in artifacting, also slower. for higher framerate input videos lower block size can be better. options:
  - 4
  - 8 (default)
  - 16
  - 32

- interpolation mask area - mask amount used when interpolating. higher values can mean static objects are blurred less, but can also result in less smooth output (moving parts of the image can be mistaken for static parts and don't get blurred)

### manual svp override

You can customise the SVP interpolation settings even further by manually defining json parameters. [see here](https://www.svp-team.com/wiki/Manual:SVPflow) for explanations on settings

- manual svp: enables manual svp settings, true/false
- super string: json string used as input in [SVSuper](https://www.svp-team.com/wiki/Manual:SVPflow#SVSuper.28source.2C_params_string.29)
- vectors string: json string used as input in [SVAnalyse](https://www.svp-team.com/wiki/Manual:SVPflow#SVAnalyse.28super.2C_params_string.2C_.5Bsrc.5D:_clip.29)
- smooth string: json string used as input in [SVSmoothFps](https://www.svp-team.com/wiki/Manual:SVPflow#SVSmoothFps.28source.2C_super.2C_vectors.2C_params_string.2C_.5Bsar.5D:_float.2C_.5Bmt.5D:_integer.29)

These options are not visible by default, add them to your config and they will be used.

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
