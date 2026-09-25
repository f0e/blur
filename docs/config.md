# Config files explained

All config files are stored in your config folder:

- Windows: `%APPDATA%\blur`
- macOS: `~/Library/Application Support/blur`
- Linux: `~/.config/blur` (or `$XDG_CONFIG_HOME/blur`)

| file                                           | purpose                                        |
| ---------------------------------------------- | ---------------------------------------------- |
| [`configs/*.cfg`](#configscfg)                 | blur configs, the settings used when rendering |
| [`rules.cfg`](#rulescfg)                       | which blur config to use for each video        |
| [`encoding-presets.cfg`](#encoding-presetscfg) | ffmpeg arguments for each encode preset        |
| [`app.cfg`](#appcfg)                           | app and pc-specific settings                   |

## configs/\*.cfg

Each file in the `configs` folder is a separate blur config.

### blur

- blur - whether or not the output video file will have motion blur
- blur amount - if blur is enabled, this is the amount of motion blur (0 = no blur, 1 = fully blend every frame together, 1+ = more blur/ghosting)
- blur output fps - if blur is enabled, this is the fps the output video will be
- blur weighting - weighting function to use when blending frames. options are listed below. also: [view weighting comparison graphs.](../tests/plot_weighting_functions/weighting_functions.pdf)
  - equal - each frame is blended equally
  - gaussian_sym
  - vegas
  - pyramid
  - gaussian
  - ascending
  - descending
  - gaussian_reverse
  - custom weights - custom comma-separated frame weights, e.g. 5, 3, 3, 2, 1. higher numbers indicate frames being more visible when blending, lower numbers mean they are less so.
- preserve brightness - blends frames by how much light they hold rather than averaging the stored values. produces a more natural-looking blur
- bloom - makes bright areas glow, brightening the pixels around them
- bloom threshold - how bright something has to be before it glows (0 = everything glows, 1 = only the very brightest)
- bloom strength - how bright the glow is

### interpolation

- interpolate - whether or not the input video file will be interpolated to a higher fps
- interpolated fps - if interpolate is enabled, this is the fps that the input file will be interpolated to (before blurring). can be a set fps number or a multiplier (append x to end e.g. `5x`)
- interpolation method - method used for interpolation (`svp`, `rife`, `rife (tensorrt)`, `mvtools`):
  - Quality: rife = rife (tensorrt) > svp
  - Speed: svp >> rife (tensorrt) > rife
  - `rife (tensorrt)` is only available with a supported nvidia gpu
  - Note: On macOS, SVP requires SVP Manager to be open or a red border will appear. It provides a 30-day trial, but then costs $24.99 for a lifetime license. RIFE can always be used however, but it is slower than SVP.

### pre-interpolation

- pre-interpolate - enable pre-interpolation using a more accurate but slower AI model before main interpolation
- pre-interpolated fps - fps to pre-interpolate the input video to (before main interpolation and blurring). can be a set fps number or a multiplier (append x to end e.g. `5x`)
- pre-interpolation method - method used for pre-interpolation (`rife`, `rife (tensorrt)`):
  - Quality: rife = rife (tensorrt)
  - Speed: rife (tensorrt) > rife

### deduplication

- deduplicate - removes duplicate frames and generates new interpolated frames to take their place. fixes 'unsmooth' looking output caused by stuttering in recordings
- deduplicate method - method used to replace duplicate frames. only used when interpolation is off, otherwise duplicates are filled by the interpolation method:
  - Quality: rife = rife (tensorrt) > svp
  - Speed: old > svp >>> rife

### masking

Masks protect regions of the frame (like a HUD) from interpolation and deduplication. Black is protected, white is interpolated as normal.

- mask - image in the `masks` folder (inside your [config folder](#config-files-explained)) to use as a mask, empty for none
- auto mask - generates an extra mask per video from the parts that never move, applied on top of `mask`. tuned in the advanced section

### rendering

- encode preset - encoding preset to use (e.g. `h264`, `h265`, `av1`). presets are defined in [`encoding-presets.cfg`](#encoding-presetscfg), and the ones available depend on whether gpu encoding is on and the `gpu type` in [`app.cfg`](#appcfg)
- quality - quality of the output video. lower = better quality (e.g. [crf](https://trac.ffmpeg.org/wiki/Encode/H.264#crf) for cpu encoding, 0 = lossless quality, 51 = really bad). the range depends on the encoder
- preview - opens a render preview window
- detailed filenames - adds blur settings to generated filenames
- copy dates - copies over the modified date from the input file to the output file
- upscale - upscales to 4K using nearest-neighbour interpolation

### rife models

- rife model - model used by `rife`. models are read from the `models` folder where blur is installed (`lib/models` on windows), drop new ones in to use them
- rife (tensorrt) model - model used by `rife (tensorrt)`

### gpu acceleration

- gpu decoding - uses gpu when decoding. can cause issues
- gpu interpolation - uses gpu when interpolating
- gpu encoding - uses gpu when rendering. the gpu type used is set in [`app.cfg`](#appcfg)

### timescale

- timescale - enable video timescale manipulation
- input timescale - timescale of the input video file (will be sped up/slowed down accordingly)
- output timescale - timescale of the output video file
- adjust timescaled audio pitch - will pitch shift audio when sped up/slowed down

### filters

- filters - enable video filters
- brightness - brightness of the output video
- saturation - saturation of the output video
- contrast - contrast of the output video

### advanced

- advanced - enables the advanced settings below. when disabled, their defaults are used

### advanced deduplication

- deduplicate range - how far apart two frames can be and still have frames generated between them (-1 = infinite). make it higher if your footage is at a lower FPS than it should be (e.g. choppy 120fps gameplay recorded at 240fps), lower it if your blurred footage starts blurring static elements such as menu screens
- deduplicate threshold - threshold of movement that triggers deduplication. turn on debug and render a video to embed text showing the movement in each frame
- deduplicate real frame - which frame in a run of duplicates is the real one:
  - first - the first frame (default)
  - last - the last frame
  - center - the middle frame
  - surrounding - ignores all the duplicates and uses the frames either side of them. gives the smoothest result but can lead to a lot of artifacting
- deduplicate max future checks - how many following runs of duplicates `surrounding` can skip over (limited by deduplicate range)

### advanced frame timing

- use frame timing logs - for use with the [obs-frame-timing-recorder](https://github.com/f0e/obs-frame-timing-recorder) OBS plugin. uses the `.frametiming` files it generates to work out when each frame was really drawn, resulting in much smoother blur. replaces deduplication, as it's more accurate

### advanced rendering

- video container - the output video container format (e.g. `mp4`, `mkv`, `avi`)
- custom ffmpeg filters - custom ffmpeg filters to be used when rendering (replaces gpu encoding, encode preset & quality options)
- debug - logs ffmpeg & vspipe commands, and adds a text overlay displaying frame similarity onto duplicate frames
- source plugin - vapoursynth plugin used to read the input video (`LWLibavSource` or `BestSource`)

### advanced masking

- auto mask samples - how many frames are sampled to find the parts that never move
- auto mask stillness - how much of the time a pixel has to stay static
- auto mask fill - how far the mask fills into a flat area from the detail around it
- auto mask padding - how far the mask spreads past what was found
- auto mask feather - how far the edge of the mask fades out

### advanced blur

- blur weighting gaussian std dev - standard deviation used in the gaussian weighting
- blur weighting gaussian mean - mean used in the gaussian weighting
- blur weighting gaussian bound - bound used in the gaussian weighting

### advanced interpolation

- svp interpolation preset - preset used for framerate interpolation when using SVP, one of:
  - weak (default) - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - film - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - smooth - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - animation - _[explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - default _(default svp settings)_
  - test
- svp interpolation algorithm - algorithm used for framerate interpolation when using SVP, one of:
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

## rules.cfg

- default config - blur config used when no rule matches. empty to pick one in the queue each time
- rule - picks a blur config for videos whose path matches a pattern. rules are checked in order and the first match wins
  - config - name of the blur config to use
  - pattern - matched against the video's full path, case insensitive. `*` matches anything and `?` matches any single character, e.g. `D:/clips/csgo/*`. without wildcards, matches any path containing the pattern

## encoding-presets.cfg

Presets are grouped by encoder (`nvidia`, `amd`, `intel`, `mac`, `cpu`), each a `name: ffmpeg arguments` line. `{quality}` is replaced with the config's `quality`.

- built-in presets are marked with `*` and can't be modified
- add your own presets under the relevant group. they need a video codec (`-c:v`) to show up as an encode preset

## app.cfg

### pc-specific blur settings

- output prefix - folder rendered videos are saved to, relative to the input video's folder
- gpu type (nvidia/amd/intel) - gpu used for gpu encoding, picks which group of encode presets is used
- rife gpu - gpu used by `rife` (auto = fastest)
- rife (tensorrt) gpu - gpu used by `rife (tensorrt)` (auto = fastest)

### gui

- window width - width of the app window
- window height - height of the app window
- dpi scale (0 = auto) - ui scale. 0 uses the os scale
- blur amount tied to fps - scales blur amount with output fps when output fps is changed, keeping the same amount of blur
- skip queue - starts rendering videos as soon as they're added instead of queueing them up

### preview

- preview volume - volume of videos previewed in the queue
- hardware accelerated preview - decodes previewed videos (in the queue and config preview) on the gpu. will usually be faster, but not always. try toggling it if the preview is choppy
- sample video path - video used to preview config changes
- config preview seek - where in the sample video to preview (0 = start, 1 = end)

### desktop notifications

- render success notifications - sends a desktop notification when a render finishes
- render failure notifications - sends a desktop notification when a render fails
- taskbar progress - shows how far along the current render is on the app's taskbar icon

### updates

- check for updates - checks for new versions on launch
- include beta updates - also notifies about beta versions
- dismissed update version - update you dismissed, so you won't be notified about it again

### linux

- vapoursynth lib path - path to your VapourSynth libraries, used if they aren't in the default location
